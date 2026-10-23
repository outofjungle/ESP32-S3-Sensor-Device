#include "sensor_ldc.h"
#include "ldc_regs.h"
#include "ldc_logic.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sensor_ldc";

// The chip's own fixed identity registers. See docs/19-sensor-ldc1612.md.
#define REG_MANUFACTURER_ID 0x7E
#define REG_DEVICE_ID       0x7F
#define LDC_MANUFACTURER_ID 0x5449
#define LDC_DEVICE_ID       0x3055

// At most this many instances of this driver at once. Small and static --
// this is a PoC with one Grove port; see docs/15-adding-a-sensor.md for why
// a sensor module owns a fixed pool rather than allocating.
#define MAX_LDC_INSTANCES 2

// ---------------------------------------------------------------------------
// Per-instance state
// ---------------------------------------------------------------------------

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    portMUX_TYPE mux;

    // The cached sample. poll() writes it; the channel get() callbacks
    // below read it. Never touched outside this pair, so a GET never
    // blocks on I2C. See docs/15-adding-a-sensor.md.
    bool has_sample[2];
    bool active[2];       // this channel is currently being converted
    uint32_t raw[2];      // the 28-bit conversion result
    uint8_t err_flags[2]; // DATAx_MSB's 4 error bits, as a nibble
    uint32_t freq_hz[2];
    bool present[2]; // this channel's firmware-computed window result
    uint16_t status; // the STATUS register, as last polled

    // Firmware-held window configuration. The LDC1612 has no window
    // hardware, unlike the VL53L1X, so these six values live here, not on
    // the chip. 0 means OFF (matching the window enum's index 0). See
    // docs/09-sensor-configuration.md, "cfg_get() and keys with no chip
    // register".
    uint8_t window[2];
    uint32_t thresh_low[2];
    uint32_t thresh_high[2];

    // Consecutive poll() failures since the last success. See ldc_poll().
    uint32_t poll_fail_count;
} ldc_ctx_t;

static ldc_ctx_t s_ctx_pool[MAX_LDC_INSTANCES];
static size_t s_ctx_used = 0;

// ---------------------------------------------------------------------------
// Register access. The LDC1612 addresses a register with one byte, unlike
// the VL53L1X's two-byte index -- see
// components/vl53l1x_uld/src/vl53l1x_platform_esp.c for the equivalent
// VL53L1X transport.
// ---------------------------------------------------------------------------

#define I2C_TIMEOUT_MS 50

static esp_err_t ldc_write16(ldc_ctx_t *ctx, uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return i2c_master_transmit(ctx->i2c_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t ldc_read16(ldc_ctx_t *ctx, uint8_t reg, uint16_t *out)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(ctx->i2c_dev, &reg, 1, rx, sizeof(rx), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    *out = ((uint16_t)rx[0] << 8) | rx[1];
    return ESP_OK;
}

static esp_err_t ldc_update_bits(ldc_ctx_t *ctx, uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t cur = 0;
    esp_err_t err = ldc_read16(ctx, reg, &cur);
    if (err != ESP_OK) {
        return err;
    }
    return ldc_write16(ctx, reg, ldc_field_merge(cur, mask, value));
}

// A bitfield read, shared by most of cfg_get() below.
static esp_err_t cfg_get_field(ldc_ctx_t *ctx, uint8_t reg, uint16_t mask, uint8_t shift, int32_t *out)
{
    uint16_t v = 0;
    esp_err_t err = ldc_read16(ctx, reg, &v);
    if (err != ESP_OK) {
        return err;
    }
    *out = ldc_field_unpack(v, mask, shift);
    return ESP_OK;
}

// A plain bitfield write, for a CONFIG/MUX_CONFIG field this firmware does
// not need to protect with the sleep bracket below.
static esp_err_t cfg_set_field(ldc_ctx_t *ctx, uint8_t reg, uint16_t mask, uint8_t shift, int32_t value)
{
    return ldc_update_bits(ctx, reg, mask, ldc_field_pack(value, mask, shift));
}

// RCOUNT, SETTLECOUNT, CLOCK_DIVIDERS, and DRIVE_CURRENT only take effect
// cleanly while the chip is not converting (CONFIG.SLEEP_MODE_EN set).
// This wraps one such write: read CONFIG, set SLEEP_MODE_EN if it was not
// already set, write the target field, then restore CONFIG's previous
// sleep state. See docs/09-sensor-configuration.md, "Ordering rules for
// the LDC1612". Takes inst, not just ctx, because a wake-up failure that
// persists past its retries faults the instance -- see the comment below.
static esp_err_t cfg_set_field_asleep(sensor_inst_t *inst, uint8_t reg, uint16_t mask, uint8_t shift, int32_t value)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    uint16_t config = 0;
    esp_err_t err = ldc_read16(ctx, REG_CONFIG, &config);
    if (err != ESP_OK) {
        return err;
    }
    bool was_awake = (config & CONFIG_SLEEP_MODE_EN_BIT) == 0;
    if (was_awake) {
        err = ldc_update_bits(ctx, REG_CONFIG, CONFIG_SLEEP_MODE_EN_BIT, CONFIG_SLEEP_MODE_EN_BIT);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = ldc_update_bits(ctx, reg, mask, ldc_field_pack(value, mask, shift));
    if (was_awake) {
        // Retry a few times before giving up: leaving the chip stuck
        // asleep -- it never converts again -- with the host seeing only
        // one failed SCFG reply and nothing after that, is worse than a
        // slightly slower write, or an explicit fault if it truly does
        // not recover. See AGENTS.md, "Firmware mistakes to avoid".
        esp_err_t wake_err = ESP_FAIL;
        for (int attempt = 0; attempt < 3; attempt++) {
            wake_err = ldc_update_bits(ctx, REG_CONFIG, CONFIG_SLEEP_MODE_EN_BIT, 0);
            if (wake_err == ESP_OK) {
                break;
            }
        }
        if (wake_err != ESP_OK) {
            ESP_LOGE(TAG, "%s: could not wake the chip after a config write; faulting the instance",
                     sensor_core_get_name(inst));
            sensor_core_set_fault(inst);
        }
        if (err == ESP_OK) {
            err = wake_err;
        }
    }
    return err;
}

// ---------------------------------------------------------------------------
// Configuration keys. See docs/09-sensor-configuration.md for the full
// table, the ordering rules, and the register map in
// docs/19-sensor-ldc1612.md.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Channel get() callbacks. ch0_raw/ch1_raw and ch0_freq_hz/ch1_freq_hz
// report CBOR null before the first conversion, on an error, or when
// autoscan is off and this is not the active channel. ch0_present/
// ch1_present report null only when that channel's own window key is
// OFF. ch0_err/ch1_err and status are never null -- see
// docs/08-channel-model-and-types.md.
// ---------------------------------------------------------------------------

static esp_err_t ch_get_raw(sensor_inst_t *inst, CborEncoder *enc, int ch)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (ctx == NULL) {
        // Defense in depth: sensor_core gates every channel get() on
        // instance state before calling here, but a driver must not
        // trust its caller either. See AGENTS.md, "Firmware mistakes to
        // avoid".
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    portENTER_CRITICAL(&ctx->mux);
    bool ok = ctx->has_sample[ch] && ctx->active[ch] && ctx->err_flags[ch] == 0;
    uint32_t raw = ctx->raw[ch];
    portEXIT_CRITICAL(&ctx->mux);
    if (!ok) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, raw));
}
static esp_err_t ch_get_ch0_raw(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_raw(inst, enc, 0);
}
static esp_err_t ch_get_ch1_raw(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_raw(inst, enc, 1);
}

static esp_err_t ch_get_freq(sensor_inst_t *inst, CborEncoder *enc, int ch)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (ctx == NULL) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    portENTER_CRITICAL(&ctx->mux);
    bool ok = ctx->has_sample[ch] && ctx->active[ch] && ctx->err_flags[ch] == 0;
    uint32_t freq = ctx->freq_hz[ch];
    portEXIT_CRITICAL(&ctx->mux);
    if (!ok) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, freq));
}
static esp_err_t ch_get_ch0_freq_hz(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_freq(inst, enc, 0);
}
static esp_err_t ch_get_ch1_freq_hz(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_freq(inst, enc, 1);
}

static esp_err_t ch_get_present(sensor_inst_t *inst, CborEncoder *enc, int ch)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (ctx == NULL) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    portENTER_CRITICAL(&ctx->mux);
    bool off = ctx->window[ch] == 0; // index 0 == "OFF" in the window enum
    bool present = ldc_present_effective(ctx->window[ch], ctx->present[ch], ctx->active[ch], ctx->has_sample[ch]);
    portEXIT_CRITICAL(&ctx->mux);
    if (off) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_boolean(enc, present));
}
static esp_err_t ch_get_ch0_present(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_present(inst, enc, 0);
}
static esp_err_t ch_get_ch1_present(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_present(inst, enc, 1);
}

static esp_err_t ch_get_err(sensor_inst_t *inst, CborEncoder *enc, int ch)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (ctx == NULL) {
        // This channel's declared type is "uint", never null -- see
        // docs/08-channel-model-and-types.md -- so the defensive fallback
        // here is 0, not null.
        return sensor_cbor_to_esp(cbor_encode_uint(enc, 0));
    }
    portENTER_CRITICAL(&ctx->mux);
    uint8_t err_flags = ctx->err_flags[ch];
    portEXIT_CRITICAL(&ctx->mux);
    return sensor_cbor_to_esp(cbor_encode_uint(enc, err_flags));
}
static esp_err_t ch_get_ch0_err(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_err(inst, enc, 0);
}
static esp_err_t ch_get_ch1_err(sensor_inst_t *inst, CborEncoder *enc)
{
    return ch_get_err(inst, enc, 1);
}

static esp_err_t ch_get_status(sensor_inst_t *inst, CborEncoder *enc)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (ctx == NULL) {
        return sensor_cbor_to_esp(cbor_encode_uint(enc, 0)); // see ch_get_err's comment
    }
    portENTER_CRITICAL(&ctx->mux);
    uint16_t status = ctx->status;
    portEXIT_CRITICAL(&ctx->mux);
    return sensor_cbor_to_esp(cbor_encode_uint(enc, status));
}

static const sensor_channel_def_t s_ldc_channels[] = {
    { "ch0_raw", "uint", "RO", ch_get_ch0_raw, NULL },
    { "ch1_raw", "uint", "RO", ch_get_ch1_raw, NULL },
    { "ch0_freq_hz", "uint", "RO", ch_get_ch0_freq_hz, NULL },
    { "ch1_freq_hz", "uint", "RO", ch_get_ch1_freq_hz, NULL },
    { "ch0_present", "bool", "RO", ch_get_ch0_present, NULL },
    { "ch1_present", "bool", "RO", ch_get_ch1_present, NULL },
    { "ch0_err", "uint", "RO", ch_get_ch0_err, NULL },
    { "ch1_err", "uint", "RO", ch_get_ch1_err, NULL },
    { "status", "uint", "RO", ch_get_status, NULL },
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static esp_err_t ldc_probe(sensor_inst_t *inst)
{
    if (s_ctx_used >= MAX_LDC_INSTANCES) {
        return ESP_ERR_NO_MEM;
    }
    // Use the next slot, but do not commit it (advance s_ctx_used) until
    // this function is actually going to succeed: a failed probe must
    // not permanently consume a pool slot, or enough failed probes
    // exhaust the pool for an instance that would otherwise work. See
    // AGENTS.md, "Firmware mistakes to avoid".
    ldc_ctx_t *ctx = &s_ctx_pool[s_ctx_used];
    memset(ctx, 0, sizeof(*ctx));
    ctx->mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    // Match the key defaults, so the first threshold write is checked
    // against the real range rather than against zero.
    ctx->thresh_high[0] = ctx->thresh_high[1] = ldc_cfgkeys[CFG_CH0_THRESH_HIGH].default_value;

    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = b->i2c_addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(b->i2c_bus, &dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device(0x%02X) failed: %d", b->i2c_addr, err);
        return err;
    }
    ctx->i2c_dev = handle;

    uint16_t mfr = 0, dev_id = 0;
    if (ldc_read16(ctx, REG_MANUFACTURER_ID, &mfr) != ESP_OK || ldc_read16(ctx, REG_DEVICE_ID, &dev_id) != ESP_OK ||
        mfr != LDC_MANUFACTURER_ID || dev_id != LDC_DEVICE_ID) {
        ESP_LOGW(TAG, "probe: manufacturer 0x%04X device 0x%04X (want 0x%04X/0x%04X)", mfr, dev_id, LDC_MANUFACTURER_ID,
                 LDC_DEVICE_ID);
        i2c_master_bus_rm_device(ctx->i2c_dev);
        ctx->i2c_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    // Set only now that probe is actually going to succeed, so a channel
    // get() callback never dereferences a NULL context -- a host can GET
    // a channel on a faulted instance (see SENS,
    // docs/09-sensor-configuration.md), and every get() here reads this
    // context. The central gate in sensor_core.c's channel_shim_get()
    // already prevents that for a fault-state instance; this is the
    // second, driver-level layer, not the only one.
    sensor_core_set_user_ctx(inst, ctx);
    s_ctx_used++;
    return ESP_OK;
}

static esp_err_t ldc_init(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);

    // Reset to the chip's power-on defaults, discarding any leftover
    // state from an earlier run -- this firmware cannot drive the chip's
    // SD pin to reset it in hardware. See docs/19-sensor-ldc1612.md. The
    // chip returns to Sleep Mode.
    if (ldc_write16(ctx, REG_RESET_DEV, 0x8000) != ESP_OK) {
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    // Two CONFIG bits this firmware fixes rather than exposes as
    // configuration keys, matching the Seeed reference design: automatic
    // amplitude correction off (the datasheet recommends this for
    // precision applications), and full-current sensor activation
    // (faster startup than the chip's reset default, low-power
    // activation). See docs/09-sensor-configuration.md.
    if (ldc_update_bits(ctx, REG_CONFIG, CONFIG_AUTO_AMP_DIS_BIT | CONFIG_SENSOR_ACTIVATE_SEL_BIT,
                        CONFIG_AUTO_AMP_DIS_BIT) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ldc_start(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    return ldc_update_bits(ctx, REG_CONFIG, CONFIG_SLEEP_MODE_EN_BIT, 0);
}

static esp_err_t ldc_stop(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    return ldc_update_bits(ctx, REG_CONFIG, CONFIG_SLEEP_MODE_EN_BIT, CONFIG_SLEEP_MODE_EN_BIT);
}

static esp_err_t read_data_channel(ldc_ctx_t *ctx, int ch, uint32_t *raw_out, uint8_t *err_out)
{
    uint8_t msb_reg = (ch == 0) ? REG_DATA0_MSB : REG_DATA1_MSB;
    uint8_t lsb_reg = (ch == 0) ? REG_DATA0_LSB : REG_DATA1_LSB;
    uint16_t msb = 0, lsb = 0;
    // DATAx_LSB must be read after DATAx_MSB, in that order, for one
    // coherent 28-bit result. See docs/19-sensor-ldc1612.md.
    esp_err_t err = ldc_read16(ctx, msb_reg, &msb);
    if (err != ESP_OK) {
        return err;
    }
    err = ldc_read16(ctx, lsb_reg, &lsb);
    if (err != ESP_OK) {
        return err;
    }
    ldc_unpack_data(msb, lsb, raw_out, err_out);
    return ESP_OK;
}

// Does the actual work; ldc_poll() below wraps this with the I2C bus
// mutex and the failure/fault bookkeeping. See AGENTS.md, "Firmware
// mistakes to avoid" -- a multi-transaction register sequence (this
// function reads up to 9 registers) must not interleave with a
// concurrent SCFG write from the USB command task; two FreeRTOS tasks
// can run at the same instant on this chip's two CPU cores.
static esp_err_t ldc_poll_locked(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);

    uint16_t config = 0, mux = 0, status = 0;
    uint16_t clkdiv[2] = { 0, 0 };
    if (ldc_read16(ctx, REG_CONFIG, &config) != ESP_OK || ldc_read16(ctx, REG_MUX_CONFIG, &mux) != ESP_OK ||
        ldc_read16(ctx, REG_STATUS, &status) != ESP_OK || ldc_read16(ctx, REG_CLOCK_DIVIDERS0, &clkdiv[0]) != ESP_OK ||
        ldc_read16(ctx, REG_CLOCK_DIVIDERS1, &clkdiv[1]) != ESP_OK) {
        return ESP_FAIL;
    }

    uint32_t f_clk = ldc_f_clk(config);
    bool active[2];
    ldc_active_channels(config, mux, active);
    uint32_t raw[2] = { 0, 0 };
    uint8_t err_flags[2] = { 0, 0 };
    uint32_t freq_hz[2] = { 0, 0 };
    bool present[2] = { false, false };

    for (int ch = 0; ch < 2; ch++) {
        if (!active[ch]) {
            continue; // not converting; its sample is left as it was
        }
        if (read_data_channel(ctx, ch, &raw[ch], &err_flags[ch]) != ESP_OK) {
            return ESP_FAIL;
        }
        freq_hz[ch] = ldc_freq_hz(raw[ch], ldc_fin_div(clkdiv[ch]), ldc_f_ref(clkdiv[ch], f_clk));

        portENTER_CRITICAL(&ctx->mux);
        uint8_t window = ctx->window[ch];
        uint32_t low = ctx->thresh_low[ch];
        uint32_t high = ctx->thresh_high[ch];
        portEXIT_CRITICAL(&ctx->mux);
        present[ch] = ldc_window_eval(window, raw[ch], low, high);
    }

    portENTER_CRITICAL(&ctx->mux);
    ctx->status = status;
    for (int ch = 0; ch < 2; ch++) {
        ctx->active[ch] = active[ch];
        if (active[ch]) {
            ctx->has_sample[ch] = true;
            ctx->raw[ch] = raw[ch];
            ctx->err_flags[ch] = err_flags[ch];
            ctx->freq_hz[ch] = freq_hz[ch];
            ctx->present[ch] = present[ch];
        }
    }
    portEXIT_CRITICAL(&ctx->mux);
    return ESP_OK;
}

static esp_err_t ldc_poll(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    if (xSemaphoreTake(b->i2c_bus_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ldc_poll_locked(inst);
    xSemaphoreGive(b->i2c_bus_mutex);

    if (result != ESP_OK) {
        // A read failed partway through this cycle. Do not leave the
        // previous sample in place to be reported as if it were still
        // fresh -- clear it. After enough consecutive failures, tell
        // sensor_core this instance can no longer be trusted, rather
        // than silently repeat a frozen last-known reading forever. See
        // AGENTS.md, "Firmware mistakes to avoid".
        portENTER_CRITICAL(&ctx->mux);
        ctx->has_sample[0] = false;
        ctx->has_sample[1] = false;
        bool give_up = ldc_note_poll(&ctx->poll_fail_count, false);
        portEXIT_CRITICAL(&ctx->mux);
        if (give_up) {
            sensor_core_set_fault(inst);
        }
    } else {
        portENTER_CRITICAL(&ctx->mux);
        ldc_note_poll(&ctx->poll_fail_count, true);
        portEXIT_CRITICAL(&ctx->mux);
    }
    return result;
}

static uint32_t channel_conversion_ms(ldc_ctx_t *ctx, uint8_t rcount_reg, uint8_t clkdiv_reg, uint32_t f_clk)
{
    uint16_t rcount = 0, clkdiv = 0;
    if (ldc_read16(ctx, rcount_reg, &rcount) != ESP_OK || ldc_read16(ctx, clkdiv_reg, &clkdiv) != ESP_OK) {
        return 10; // a transient read failure should not stall the poll task
    }
    return ldc_conv_ms(rcount, clkdiv, f_clk);
}

// Does the actual work; ldc_poll_interval_ms() below wraps this with the
// I2C bus mutex. See ldc_poll_locked's comment.
static uint32_t ldc_poll_interval_ms_locked(sensor_inst_t *inst)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    uint16_t config = 0, mux = 0;
    if (ldc_read16(ctx, REG_CONFIG, &config) != ESP_OK || ldc_read16(ctx, REG_MUX_CONFIG, &mux) != ESP_OK) {
        return 10;
    }
    uint32_t f_clk = ldc_f_clk(config);
    bool autoscan = (mux & MUX_CONFIG_AUTOSCAN_EN_BIT) != 0;

    uint32_t ch0_ms = channel_conversion_ms(ctx, REG_RCOUNT0, REG_CLOCK_DIVIDERS0, f_clk);
    uint32_t ch1_ms = autoscan ? channel_conversion_ms(ctx, REG_RCOUNT1, REG_CLOCK_DIVIDERS1, f_clk) : 0;
    return ldc_interval_ms(ch0_ms, ch1_ms, autoscan);
}

static uint32_t ldc_poll_interval_ms(sensor_inst_t *inst)
{
    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    if (xSemaphoreTake(b->i2c_bus_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 10; // a transient lock-contention failure should not stall the poll task
    }
    uint32_t ms = ldc_poll_interval_ms_locked(inst);
    xSemaphoreGive(b->i2c_bus_mutex);
    return ms;
}

// ---------------------------------------------------------------------------
// Configuration: get always reads the chip, except the six firmware-held
// window/threshold keys, which have no chip register -- see
// docs/09-sensor-configuration.md. set enforces the chip's own ordering
// rules by re-asserting the related register(s) on every write.
// ---------------------------------------------------------------------------

static esp_err_t get_held(ldc_ctx_t *ctx, ldc_held_t which, int ch, int32_t *out)
{
    portENTER_CRITICAL(&ctx->mux);
    switch (which) {
    case LDC_HELD_WINDOW:
        *out = ctx->window[ch];
        break;
    case LDC_HELD_THRESH_LOW:
        *out = (int32_t)ctx->thresh_low[ch];
        break;
    default:
        *out = (int32_t)ctx->thresh_high[ch];
        break;
    }
    portEXIT_CRITICAL(&ctx->mux);
    return ESP_OK;
}

static esp_err_t get_deglitch(ldc_ctx_t *ctx, int32_t *out)
{
    uint16_t v = 0;
    esp_err_t err = ldc_read16(ctx, REG_MUX_CONFIG, &v);
    if (err != ESP_OK) {
        return err;
    }
    int index = ldc_deglitch_index(v & MUX_CONFIG_DEGLITCH_MASK);
    if (index < 0) {
        return ESP_FAIL; // this firmware only ever writes one of the four known codes
    }
    *out = index;
    return ESP_OK;
}

// Does the actual work; ldc_cfg_get() below wraps this with the I2C bus
// mutex. See ldc_poll_locked's comment.
static esp_err_t ldc_cfg_get_locked(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t *out)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    size_t idx = (size_t)(def - ldc_cfgkeys);

    const ldc_field_t *f = ldc_field_for_key(idx);
    if (f != NULL) {
        return cfg_get_field(ctx, f->reg, f->mask, f->shift, out);
    }
    ldc_held_t which;
    int ch;
    if (ldc_held_key(idx, &which, &ch)) {
        return get_held(ctx, which, ch, out);
    }
    return idx == CFG_DEGLITCH ? get_deglitch(ctx, out) : ESP_ERR_INVALID_ARG;
}

static esp_err_t ldc_cfg_get(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t *out)
{
    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    if (xSemaphoreTake(b->i2c_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ldc_cfg_get_locked(inst, def, out);
    xSemaphoreGive(b->i2c_bus_mutex);
    return err;
}

// Writes one firmware-held threshold, refusing a low value above the high
// one (or the reverse), as sensor_tof does. Set the high threshold first
// when moving the whole range up, the low one first when moving it down.
static esp_err_t set_threshold(ldc_ctx_t *ctx, int ch, bool is_high, uint32_t value)
{
    esp_err_t err = ESP_OK;
    portENTER_CRITICAL(&ctx->mux);
    uint32_t low = is_high ? ctx->thresh_low[ch] : value;
    uint32_t high = is_high ? value : ctx->thresh_high[ch];
    if (!ldc_thresholds_ok(low, high)) {
        err = ESP_ERR_INVALID_ARG;
    } else if (is_high) {
        ctx->thresh_high[ch] = value;
    } else {
        ctx->thresh_low[ch] = value;
    }
    portEXIT_CRITICAL(&ctx->mux);
    return err;
}

// Writes a flag that must not be on together with another one (autoscan and
// high_current): turning it on first checks the other register. Turning it off
// is always allowed. See docs/09-sensor-configuration.md.
static esp_err_t set_exclusive_flag(ldc_ctx_t *ctx, const ldc_field_t *f, int32_t value, uint8_t other_reg,
                                    bool (*allowed)(uint16_t other_value))
{
    if (value) {
        uint16_t other = 0;
        esp_err_t err = ldc_read16(ctx, other_reg, &other);
        if (err != ESP_OK) {
            return err;
        }
        if (!allowed(other)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return cfg_set_field(ctx, f->reg, f->mask, f->shift, value ? 1 : 0);
}

static esp_err_t set_held(ldc_ctx_t *ctx, ldc_held_t which, int ch, int32_t value)
{
    switch (which) {
    case LDC_HELD_WINDOW:
        portENTER_CRITICAL(&ctx->mux);
        ctx->window[ch] = (uint8_t)value;
        portEXIT_CRITICAL(&ctx->mux);
        return ESP_OK;
    case LDC_HELD_THRESH_LOW:
        return set_threshold(ctx, ch, false, (uint32_t)value);
    default:
        return set_threshold(ctx, ch, true, (uint32_t)value);
    }
}

static esp_err_t set_deglitch(ldc_ctx_t *ctx, int32_t value)
{
    // sensor_core already validates an enum value's range before calling
    // cfg_set (both from the wire and from a loaded NVS value), but a driver
    // must not trust its caller either -- see AGENTS.md, "Firmware mistakes
    // to avoid".
    int code = ldc_deglitch_code(value);
    if (code < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ldc_update_bits(ctx, REG_MUX_CONFIG, MUX_CONFIG_DEGLITCH_MASK, (uint16_t)code);
}

static esp_err_t set_field_by_kind(sensor_inst_t *inst, ldc_ctx_t *ctx, size_t idx, const ldc_field_t *f, int32_t value)
{
    switch (f->write) {
    case LDC_WRITE_ASLEEP:
        return cfg_set_field_asleep(inst, f->reg, f->mask, f->shift, value);
    case LDC_WRITE_CHECKED:
        // autoscan is refused while high_current is set, and the reverse.
        return idx == CFG_AUTOSCAN ? set_exclusive_flag(ctx, f, value, REG_CONFIG, ldc_autoscan_allowed)
                                   : set_exclusive_flag(ctx, f, value, REG_MUX_CONFIG, ldc_high_current_allowed);
    default:
        return cfg_set_field(ctx, f->reg, f->mask, f->shift, value);
    }
}

// Does the actual work; ldc_cfg_set() below wraps this with the I2C bus
// mutex. See ldc_poll_locked's comment.
static esp_err_t ldc_cfg_set_locked(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t value)
{
    ldc_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    size_t idx = (size_t)(def - ldc_cfgkeys);

    const ldc_field_t *f = ldc_field_for_key(idx);
    if (f != NULL) {
        return set_field_by_kind(inst, ctx, idx, f, value);
    }
    ldc_held_t which;
    int ch;
    if (ldc_held_key(idx, &which, &ch)) {
        return set_held(ctx, which, ch, value);
    }
    return idx == CFG_DEGLITCH ? set_deglitch(ctx, value) : ESP_ERR_INVALID_ARG;
}

static esp_err_t ldc_cfg_set(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t value)
{
    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    if (xSemaphoreTake(b->i2c_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ldc_cfg_set_locked(inst, def, value);
    xSemaphoreGive(b->i2c_bus_mutex);
    return err;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

static const sensor_driver_t s_ldc_driver = {
    .type_name = "ldc1612",
    .channels = s_ldc_channels,
    .channel_count = sizeof(s_ldc_channels) / sizeof(s_ldc_channels[0]),
    .cfgkeys = ldc_cfgkeys,
    .cfgkey_count = CFG_COUNT,
    .probe = ldc_probe,
    .init = ldc_init,
    .start = ldc_start,
    .stop = ldc_stop,
    .poll = ldc_poll,
    .poll_interval_ms = ldc_poll_interval_ms,
    .cfg_get = ldc_cfg_get,
    .cfg_set = ldc_cfg_set,
    .action = NULL, // sensor_ldc defines no action; CAL ldc0.* fails ENOCH
};

esp_err_t sensor_ldc_register(const char *instance_name, const sensor_board_binding_t *binding)
{
    return sensor_core_register(&s_ldc_driver, instance_name, binding, NULL);
}

bool sensor_ldc_is_present(void)
{
    if (s_ctx_used == 0) {
        return false;
    }
    ldc_ctx_t *ctx = &s_ctx_pool[0];
    portENTER_CRITICAL(&ctx->mux);
    bool v = ldc_any_present(ctx->window, ctx->present, ctx->active, ctx->has_sample);
    portEXIT_CRITICAL(&ctx->mux);
    return v;
}
