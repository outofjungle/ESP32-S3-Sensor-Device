#include "sensor_tof.h"
#include "tof_logic.h"
#include "VL53L1X_api.h"
#include "VL53L1X_calibration.h"
#include "vl53l1x_platform_esp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "sensor_tof";

// ST's reference part identifier for the VL53L1X family. Read from
// VL53L1_IDENTIFICATION__MODEL_ID; see docs/03-sensor-vl53l1x.md.
#define VL53L1X_EXPECTED_SENSOR_ID 0xEACC

// At most this many instances of this driver at once. Small and static --
// this is a PoC with one Grove port; see docs/15-adding-a-sensor.md for why
// a sensor module owns a fixed pool rather than allocating.
#define MAX_TOF_INSTANCES 2

// ---------------------------------------------------------------------------
// Per-instance state
// ---------------------------------------------------------------------------

typedef struct {
    portMUX_TYPE mux;

    // The cached sample. poll() writes it; the channel get() callbacks
    // below read it. Never touched outside this pair, so a GET never
    // blocks on I2C. See docs/15-adding-a-sensor.md.
    bool has_sample;
    uint16_t dist_mm;
    uint8_t status;   // VL53L1X_Result_t.Status -- 0 is a valid reading
    uint16_t signal;  // SigPerSPAD, kcps
    uint16_t ambient; // kcps
    uint16_t spads;
    bool present;    // the window-detection interrupt condition
    bool window_off; // true: window detection is off (see "window" key).
                     // Derived fresh from the chip on every poll() --
                     // see tof_poll(), not shadowed from a config
                     // write. See AGENTS.md, "Firmware mistakes to
                     // avoid".

    // Held by poll() for the duration of each cycle's I2C transactions,
    // and by a CAL action for the duration of the calibration routine, so
    // the two never interleave their reads and writes to the same chip
    // registers. poll() takes this with a zero timeout and simply skips
    // its cycle if a calibration is in progress, rather than block the
    // poll task. See docs/09-sensor-configuration.md.
    SemaphoreHandle_t cal_mutex;

    // The last value written for the two keys the chip stores in a reduced
    // form, so a read-back that matches the rounded write is reported as
    // the requested value. See tof_readback().
    bool xtalk_requested, signal_requested;
    int32_t xtalk_req, signal_req;
} tof_ctx_t;

static tof_ctx_t s_ctx_pool[MAX_TOF_INSTANCES];
static size_t s_ctx_used = 0;

// ---------------------------------------------------------------------------
// Channel get() callbacks. Every channel can report CBOR null, meaning "no
// reading right now" -- before the first poll, or (dist/status/signal/
// ambient/spads) when the range status is not 0, or (present) when window
// detection is off. See docs/08-channel-model-and-types.md.
// ---------------------------------------------------------------------------

static esp_err_t ch_get_dist(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    uint8_t status = ctx->status;
    uint16_t dist = ctx->dist_mm;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has || status != 0) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, dist));
}

static esp_err_t ch_get_present(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    bool off = ctx->window_off;
    bool present = ctx->present;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has || off) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_boolean(enc, present));
}

static esp_err_t ch_get_status(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    uint8_t status = ctx->status;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, status));
}

static esp_err_t ch_get_signal(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    uint16_t v = ctx->signal;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, v));
}

static esp_err_t ch_get_ambient(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    uint16_t v = ctx->ambient;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, v));
}

static esp_err_t ch_get_spads(sensor_inst_t *inst, CborEncoder *enc)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    portENTER_CRITICAL(&ctx->mux);
    bool has = ctx->has_sample;
    uint16_t v = ctx->spads;
    portEXIT_CRITICAL(&ctx->mux);
    if (!has) {
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return sensor_cbor_to_esp(cbor_encode_uint(enc, v));
}

static const sensor_channel_def_t s_tof_channels[] = {
    { "dist", "uint", "RO", ch_get_dist, NULL },       { "present", "bool", "RO", ch_get_present, NULL },
    { "status", "uint", "RO", ch_get_status, NULL },   { "signal", "uint", "RO", ch_get_signal, NULL },
    { "ambient", "uint", "RO", ch_get_ambient, NULL }, { "spads", "uint", "RO", ch_get_spads, NULL },
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static esp_err_t tof_probe(sensor_inst_t *inst)
{
    const sensor_board_binding_t *b = sensor_core_get_binding(inst);
    esp_err_t err = vl53l1x_platform_attach(b->i2c_bus, b->i2c_addr);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t id = 0;
    VL53L1X_ERROR vlerr = VL53L1X_GetSensorId(b->i2c_addr, &id);
    if (vlerr != 0 || id != VL53L1X_EXPECTED_SENSOR_ID) {
        ESP_LOGW(TAG, "probe: unexpected sensor id 0x%04X (want 0x%04X), I2C status %d", id, VL53L1X_EXPECTED_SENSOR_ID,
                 vlerr);
        vl53l1x_platform_detach(b->i2c_addr);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t tof_init(sensor_inst_t *inst)
{
    if (s_ctx_used >= MAX_TOF_INSTANCES) {
        return ESP_ERR_NO_MEM;
    }
    tof_ctx_t *ctx = &s_ctx_pool[s_ctx_used++];
    memset(ctx, 0, sizeof(*ctx));
    ctx->mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ctx->cal_mutex = xSemaphoreCreateMutex();
    if (ctx->cal_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    // Corrected to the chip's true state on the first poll() -- see
    // tof_poll()'s self-healing read of this same bit.
    ctx->window_off = true;
    sensor_core_set_user_ctx(inst, ctx);

    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;
    // VL53L1X_SensorInit() pushes the default register configuration, then
    // polls CheckForDataReady for up to 1 second on its own -- no separate
    // boot-wait loop is needed here. See VL53L1X_api.c.
    if (VL53L1X_SensorInit(dev) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t tof_start(sensor_inst_t *inst)
{
    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;
    return (VL53L1X_StartRanging(dev) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t tof_stop(sensor_inst_t *inst)
{
    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;
    return (VL53L1X_StopRanging(dev) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t tof_poll(sensor_inst_t *inst)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (xSemaphoreTake(ctx->cal_mutex, 0) != pdTRUE) {
        return ESP_OK; // a CAL action owns the bus right now; see tof_action()
    }
    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;

    uint8_t ready = 0;
    if (VL53L1X_CheckForDataReady(dev, &ready) != 0) {
        xSemaphoreGive(ctx->cal_mutex);
        return ESP_FAIL;
    }
    bool present_now = ready != 0;

    VL53L1X_Result_t res;
    if (VL53L1X_GetResult(dev, &res) != 0) {
        xSemaphoreGive(ctx->cal_mutex);
        return ESP_FAIL;
    }
    VL53L1X_ClearInterrupt(dev);

    // window_off, read fresh from the chip every cycle, rather than
    // shadowed from the last SCFG write: a config write that only
    // partially applies (one of SetDistanceThreshold's three I2C writes
    // fails) would otherwise leave the shadow wrong forever, with no
    // visible error. Fail safe (treat as off) if this read itself fails,
    // rather than trust a stale value. See AGENTS.md, "Firmware mistakes
    // to avoid".
    uint8_t raw_cfg_reg = 0;
    bool window_off_now = true;
    if (VL53L1_RdByte(dev, SYSTEM__INTERRUPT_CONFIG_GPIO, &raw_cfg_reg) == 0) {
        window_off_now = tof_window_is_off(raw_cfg_reg);
    }

    portENTER_CRITICAL(&ctx->mux);
    ctx->has_sample = true;
    ctx->dist_mm = res.Distance;
    ctx->status = res.Status;
    ctx->signal = res.SigPerSPAD;
    ctx->ambient = res.Ambient;
    ctx->spads = res.NumSPADs;
    ctx->present = present_now;
    ctx->window_off = window_off_now;
    portEXIT_CRITICAL(&ctx->mux);
    xSemaphoreGive(ctx->cal_mutex);
    return ESP_OK;
}

static uint32_t tof_poll_interval(sensor_inst_t *inst)
{
    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;
    uint16_t intermeas_ms = 100;
    bool ok = VL53L1X_GetInterMeasurementInMs(dev, &intermeas_ms) == 0;
    return tof_poll_interval_ms(intermeas_ms, ok);
}

// ---------------------------------------------------------------------------
// Configuration: get always reads the chip; set enforces ST's ordering
// rules by re-asserting the related register(s) on every write, so the
// chip stays consistent no matter what order a host sends keys in. See
// docs/09-sensor-configuration.md.
// ---------------------------------------------------------------------------

// One getter and one setter per key, in tof_cfg_getters / tof_cfg_setters below.
typedef esp_err_t (*tof_get_fn)(sensor_inst_t *inst, uint16_t dev, int32_t *out);
typedef esp_err_t (*tof_set_fn)(sensor_inst_t *inst, uint16_t dev, int32_t value);

static esp_err_t st_ok(VL53L1X_ERROR st)
{
    return st == 0 ? ESP_OK : ESP_FAIL;
}

// Reads a key that one ST call returns as a uint16_t.
static esp_err_t get_u16(uint16_t dev, VL53L1X_ERROR (*st_get)(uint16_t, uint16_t *), int32_t *out)
{
    uint16_t v = 0;
    if (st_get(dev, &v)) {
        return ESP_FAIL;
    }
    *out = v;
    return ESP_OK;
}

static esp_err_t get_dist_mode(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    uint16_t m = 0;
    if (VL53L1X_GetDistanceMode(dev, &m)) {
        return ESP_FAIL;
    }
    *out = tof_dist_mode_index(m);
    return ESP_OK;
}

static esp_err_t get_budget(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_u16(dev, VL53L1X_GetTimingBudgetInMs, out);
}

static esp_err_t get_intermeas(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_u16(dev, VL53L1X_GetInterMeasurementInMs, out);
}

static esp_err_t get_roi(uint16_t dev, bool want_x, int32_t *out)
{
    uint16_t x = 0, y = 0;
    if (VL53L1X_GetROI_XY(dev, &x, &y)) {
        return ESP_FAIL;
    }
    *out = want_x ? x : y;
    return ESP_OK;
}

static esp_err_t get_roi_x(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_roi(dev, true, out);
}

static esp_err_t get_roi_y(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_roi(dev, false, out);
}

static esp_err_t get_roi_center(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    uint8_t c = 0;
    if (VL53L1X_GetROICenter(dev, &c)) {
        return ESP_FAIL;
    }
    *out = c;
    return ESP_OK;
}

static esp_err_t get_offset(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    int16_t o = 0;
    if (VL53L1X_GetOffset(dev, &o)) {
        return ESP_FAIL;
    }
    *out = o;
    return ESP_OK;
}

static esp_err_t get_xtalk(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    uint16_t x = 0;
    if (VL53L1X_GetXtalk(dev, &x)) {
        return ESP_FAIL;
    }
    *out = tof_readback(ctx->xtalk_requested, ctx->xtalk_req, x,
                        ctx->xtalk_requested ? tof_xtalk_quantize((uint16_t)ctx->xtalk_req) : 0);
    return ESP_OK;
}

static esp_err_t get_sigma(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_u16(dev, VL53L1X_GetSigmaThreshold, out);
}

static esp_err_t get_signal(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    uint16_t s = 0;
    if (VL53L1X_GetSignalThreshold(dev, &s)) {
        return ESP_FAIL;
    }
    *out = tof_readback(ctx->signal_requested, ctx->signal_req, s,
                        ctx->signal_requested ? tof_signal_quantize((uint16_t)ctx->signal_req) : 0);
    return ESP_OK;
}

static esp_err_t get_window(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    // Not VL53L1X_GetDistanceThresholdWindow(): it masks the register with 0x7
    // and cannot tell "OFF" (bit 0x20 set, by us, to restore the chip's
    // plain-data-ready reset value) from "BELOW" (bit 0x20 clear, low bits
    // happen to be 0). Reading the raw register and checking bit 0x20 first is
    // the only chip-truthful way to answer this. See
    // docs/09-sensor-configuration.md.
    uint8_t raw = 0;
    if (VL53L1_RdByte(dev, SYSTEM__INTERRUPT_CONFIG_GPIO, &raw)) {
        return ESP_FAIL;
    }
    *out = tof_window_decode(raw);
    return ESP_OK;
}

static esp_err_t get_thresh_low(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_u16(dev, VL53L1X_GetDistanceThresholdLow, out);
}

static esp_err_t get_thresh_high(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    return get_u16(dev, VL53L1X_GetDistanceThresholdHigh, out);
}

static esp_err_t get_polarity(sensor_inst_t *inst, uint16_t dev, int32_t *out)
{
    (void)inst;
    uint8_t p = 0;
    if (VL53L1X_GetInterruptPolarity(dev, &p)) {
        return ESP_FAIL;
    }
    *out = tof_polarity_index(p);
    return ESP_OK;
}

static const tof_get_fn tof_cfg_getters[CFG_COUNT] = {
    [CFG_DIST_MODE] = get_dist_mode,
    [CFG_BUDGET_MS] = get_budget,
    [CFG_INTERMEAS_MS] = get_intermeas,
    [CFG_ROI_X] = get_roi_x,
    [CFG_ROI_Y] = get_roi_y,
    [CFG_ROI_CENTER] = get_roi_center,
    [CFG_OFFSET_MM] = get_offset,
    [CFG_XTALK_CPS] = get_xtalk,
    [CFG_SIGMA_MM] = get_sigma,
    [CFG_SIGNAL_KCPS] = get_signal,
    [CFG_WINDOW] = get_window,
    [CFG_THRESH_LOW_MM] = get_thresh_low,
    [CFG_THRESH_HIGH_MM] = get_thresh_high,
    [CFG_INT_POLARITY] = get_polarity,
};

static esp_err_t tof_cfg_get(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t *out)
{
    size_t idx = (size_t)(def - tof_cfgkeys);
    if (idx >= CFG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return tof_cfg_getters[idx](inst, sensor_core_get_binding(inst)->i2c_addr, out);
}

static esp_err_t set_dist_mode(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    uint16_t mode = tof_uld_dist_mode(value);
    if (VL53L1X_SetDistanceMode(dev, mode)) {
        return ESP_FAIL;
    }
    // Re-assert the current timing budget: ST's user manual requires
    // SetDistanceMode to be called before SetTimingBudgetInMs. Doing this on
    // every dist_mode change, unconditionally, makes the result correct
    // regardless of which order the host changed the two keys in.
    uint16_t budget = 50;
    VL53L1X_GetTimingBudgetInMs(dev, &budget);
    VL53L1X_SetTimingBudgetInMs(dev, tof_budget_for_mode(budget, mode)); // 15 ms is valid in SHORT mode only
    return ESP_OK;
}

static esp_err_t set_budget(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    uint16_t mode = 2;
    VL53L1X_GetDistanceMode(dev, &mode);
    if (value < 0 || value > UINT16_MAX || !tof_budget_allowed((uint16_t)value, mode)) {
        return ESP_ERR_INVALID_ARG; // not an allowed budget, or 15 ms outside SHORT mode
    }
    VL53L1X_SetDistanceMode(dev, mode); // re-assert ordering
    return st_ok(VL53L1X_SetTimingBudgetInMs(dev, (uint16_t)value));
}

static esp_err_t set_intermeas(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    uint16_t mode = 2, budget = 50;
    VL53L1X_GetDistanceMode(dev, &mode);
    VL53L1X_GetTimingBudgetInMs(dev, &budget);
    if (!tof_intermeas_ok((uint32_t)value, budget)) {
        return ESP_ERR_INVALID_ARG; // the inter-measurement period cannot be shorter than the budget
    }
    VL53L1X_SetDistanceMode(dev, mode);
    VL53L1X_SetTimingBudgetInMs(dev, budget);
    return st_ok(VL53L1X_SetInterMeasurementInMs(dev, (uint16_t)value));
}

// ST also forces the centre to 199 for a region over 10; see
// docs/09-sensor-configuration.md.
static esp_err_t set_roi(uint16_t dev, bool is_x, int32_t value)
{
    uint16_t x = 16, y = 16;
    VL53L1X_GetROI_XY(dev, &x, &y);
    return st_ok(is_x ? VL53L1X_SetROI(dev, (uint16_t)value, y) : VL53L1X_SetROI(dev, x, (uint16_t)value));
}

static esp_err_t set_roi_x(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return set_roi(dev, true, value);
}

static esp_err_t set_roi_y(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return set_roi(dev, false, value);
}

static esp_err_t set_roi_center(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return st_ok(VL53L1X_SetROICenter(dev, (uint8_t)value));
}

static esp_err_t set_offset(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return st_ok(VL53L1X_SetOffset(dev, (int16_t)value));
}

static esp_err_t set_xtalk(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (VL53L1X_SetXtalk(dev, (uint16_t)value)) {
        return ESP_FAIL;
    }
    ctx->xtalk_req = value;
    ctx->xtalk_requested = true;
    return ESP_OK;
}

static esp_err_t set_sigma(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return st_ok(VL53L1X_SetSigmaThreshold(dev, (uint16_t)value));
}

static esp_err_t set_signal(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    if (VL53L1X_SetSignalThreshold(dev, (uint16_t)value)) {
        return ESP_FAIL;
    }
    ctx->signal_req = value;
    ctx->signal_requested = true;
    return ESP_OK;
}

static esp_err_t set_window(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    if (value == 0) {
        // Restore the chip's reset value for this register: plain "new sample
        // ready" interrupt behavior, window detection off. See
        // docs/09-sensor-configuration.md.
        return st_ok(VL53L1_WrByte(dev, SYSTEM__INTERRUPT_CONFIG_GPIO, 0x20));
    }
    uint16_t low = 50, high = 1500;
    VL53L1X_GetDistanceThresholdLow(dev, &low);
    VL53L1X_GetDistanceThresholdHigh(dev, &high);
    // IntOnNoTarget is always 0: ST's own ULD header says this argument is
    // "no longer used" in this driver version. See
    // docs/09-sensor-configuration.md.
    return st_ok(VL53L1X_SetDistanceThreshold(dev, low, high, tof_window_mode(value), 0));
}

static esp_err_t set_threshold(uint16_t dev, bool is_high, int32_t value)
{
    uint16_t low = 50, high = 1500;
    VL53L1X_GetDistanceThresholdLow(dev, &low);
    VL53L1X_GetDistanceThresholdHigh(dev, &high);
    if (is_high) {
        high = (uint16_t)value;
    } else {
        low = (uint16_t)value;
    }
    if (!tof_thresh_ok(low, high)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t raw = 0;
    VL53L1_RdByte(dev, SYSTEM__INTERRUPT_CONFIG_GPIO, &raw);
    if (!tof_window_is_off(raw)) {
        return st_ok(VL53L1X_SetDistanceThreshold(dev, low, high, raw & 0x3, 0));
    }
    // Window detection is off. VL53L1X_SetDistanceThreshold() always turns it
    // on as a side effect (it clears bit 0x20 unconditionally), which a plain
    // threshold edit must not do. Update the threshold registers directly.
    if (VL53L1_WrWord(dev, SYSTEM__THRESH_HIGH, high)) {
        return ESP_FAIL;
    }
    return st_ok(VL53L1_WrWord(dev, SYSTEM__THRESH_LOW, low));
}

static esp_err_t set_thresh_low(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return set_threshold(dev, false, value);
}

static esp_err_t set_thresh_high(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return set_threshold(dev, true, value);
}

static esp_err_t set_polarity(sensor_inst_t *inst, uint16_t dev, int32_t value)
{
    (void)inst;
    return st_ok(VL53L1X_SetInterruptPolarity(dev, tof_uld_polarity(value)));
}

static const tof_set_fn tof_cfg_setters[CFG_COUNT] = {
    [CFG_DIST_MODE] = set_dist_mode,
    [CFG_BUDGET_MS] = set_budget,
    [CFG_INTERMEAS_MS] = set_intermeas,
    [CFG_ROI_X] = set_roi_x,
    [CFG_ROI_Y] = set_roi_y,
    [CFG_ROI_CENTER] = set_roi_center,
    [CFG_OFFSET_MM] = set_offset,
    [CFG_XTALK_CPS] = set_xtalk,
    [CFG_SIGMA_MM] = set_sigma,
    [CFG_SIGNAL_KCPS] = set_signal,
    [CFG_WINDOW] = set_window,
    [CFG_THRESH_LOW_MM] = set_thresh_low,
    [CFG_THRESH_HIGH_MM] = set_thresh_high,
    [CFG_INT_POLARITY] = set_polarity,
};

static esp_err_t tof_cfg_set(sensor_inst_t *inst, const sensor_cfgkey_def_t *def, int32_t value)
{
    size_t idx = (size_t)(def - tof_cfgkeys);
    if (idx >= CFG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return tof_cfg_setters[idx](inst, sensor_core_get_binding(inst)->i2c_addr, value);
}

// ---------------------------------------------------------------------------
// Calibration (CAL tof0.offset / CAL tof0.xtalk). See
// docs/09-sensor-configuration.md: ST recommends a 17% grey target, 100 mm
// for offset and the sensor's own "inflection point" distance for xtalk.
// ---------------------------------------------------------------------------

static esp_err_t tof_action(sensor_inst_t *inst, const char *action_name, int32_t arg, int32_t *result)
{
    tof_ctx_t *ctx = sensor_core_get_user_ctx(inst);
    uint16_t dev = sensor_core_get_binding(inst)->i2c_addr;

    tof_cal_kind_t kind = tof_cal_validate(action_name, arg);
    if (kind == TOF_CAL_UNKNOWN) {
        return ESP_ERR_NOT_FOUND;
    }
    if (kind == TOF_CAL_BAD_ARG) {
        // sensor_core cannot validate an action argument generically. Outside
        // the safe range the vendored routines hit undefined behavior. See
        // AGENTS.md, "Firmware mistakes to avoid".
        return ESP_ERR_INVALID_ARG;
    }
    bool is_offset = (kind == TOF_CAL_OFFSET);

    // Real exclusion against the poll task, not an advisory flag: without
    // this, a poll cycle landing mid-calibration can steal or duplicate
    // one of the calibration routine's own measurements, producing a
    // wrong offset or crosstalk value that a later SAVE then persists
    // with no error anywhere. See AGENTS.md, "Firmware mistakes to
    // avoid".
    if (xSemaphoreTake(ctx->cal_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret;
    if (is_offset) {
        int16_t offset = 0;
        ret = (VL53L1X_CalibrateOffset(dev, (uint16_t)arg, &offset) == 0) ? ESP_OK : ESP_FAIL;
        *result = offset;
    } else {
        uint16_t xtalk = 0;
        ret = (VL53L1X_CalibrateXtalk(dev, (uint16_t)arg, &xtalk) == 0) ? ESP_OK : ESP_FAIL;
        *result = xtalk;
    }
    // Both calibration routines leave the chip ranging (they call
    // VL53L1X_StartRanging() themselves); nothing else to restart here.
    xSemaphoreGive(ctx->cal_mutex);
    return ret;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

static const sensor_driver_t s_tof_driver = {
    .type_name = "vl53l1x",
    .channels = s_tof_channels,
    .channel_count = sizeof(s_tof_channels) / sizeof(s_tof_channels[0]),
    .cfgkeys = tof_cfgkeys,
    .cfgkey_count = CFG_COUNT,
    .probe = tof_probe,
    .init = tof_init,
    .start = tof_start,
    .stop = tof_stop,
    .poll = tof_poll,
    .poll_interval_ms = tof_poll_interval,
    .cfg_get = tof_cfg_get,
    .cfg_set = tof_cfg_set,
    .action = tof_action,
};

esp_err_t sensor_tof_register(const char *instance_name, const sensor_board_binding_t *binding)
{
    return sensor_core_register(&s_tof_driver, instance_name, binding, NULL);
}

bool sensor_tof_is_present(void)
{
    if (s_ctx_used == 0) {
        return false;
    }
    tof_ctx_t *ctx = &s_ctx_pool[0];
    portENTER_CRITICAL(&ctx->mux);
    bool v = ctx->has_sample && !ctx->window_off && ctx->present;
    portEXIT_CRITICAL(&ctx->mux);
    return v;
}
