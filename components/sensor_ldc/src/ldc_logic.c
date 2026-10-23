#include "ldc_logic.h"
#include "ldc_regs.h"

static const char *const s_window_values[] = { "OFF", "BELOW", "ABOVE", "OUT", "IN" };
static const char *const s_active_chan_values[] = { "CH0", "CH1" };
static const char *const s_deglitch_values[] = { "1MHZ", "3P3MHZ", "10MHZ", "33MHZ" };
static const char *const s_ref_clk_src_values[] = { "INTERNAL", "CLKIN" };

const sensor_cfgkey_def_t ldc_cfgkeys[CFG_COUNT] = {
    [CFG_CH0_RCOUNT] = { "ch0_rcount", SENSOR_VAL_UINT, 4, 65535, NULL, 0, 1350, NULL },
    [CFG_CH1_RCOUNT] = { "ch1_rcount", SENSOR_VAL_UINT, 4, 65535, NULL, 0, 1350, NULL },
    [CFG_CH0_SETTLECOUNT] = { "ch0_settlecount", SENSOR_VAL_UINT, 2, 65535, NULL, 0, 30, NULL },
    [CFG_CH1_SETTLECOUNT] = { "ch1_settlecount", SENSOR_VAL_UINT, 2, 65535, NULL, 0, 30, NULL },
    [CFG_CH0_FIN_DIV] = { "ch0_fin_div", SENSOR_VAL_UINT, 1, 15, NULL, 0, 1, NULL },
    [CFG_CH1_FIN_DIV] = { "ch1_fin_div", SENSOR_VAL_UINT, 1, 15, NULL, 0, 1, NULL },
    [CFG_CH0_FREF_DIV] = { "ch0_fref_div", SENSOR_VAL_UINT, 1, 1023, NULL, 0, 2, NULL },
    [CFG_CH1_FREF_DIV] = { "ch1_fref_div", SENSOR_VAL_UINT, 1, 1023, NULL, 0, 2, NULL },
    [CFG_CH0_IDRIVE] = { "ch0_idrive", SENSOR_VAL_UINT, 0, 31, NULL, 0, 20, NULL },
    [CFG_CH1_IDRIVE] = { "ch1_idrive", SENSOR_VAL_UINT, 0, 31, NULL, 0, 20, NULL },
    [CFG_CH0_WINDOW] = { "ch0_window", SENSOR_VAL_ENUM, 0, 0, s_window_values, 5, 0, NULL },
    [CFG_CH1_WINDOW] = { "ch1_window", SENSOR_VAL_ENUM, 0, 0, s_window_values, 5, 0, NULL },
    [CFG_CH0_THRESH_LOW] = { "ch0_thresh_low", SENSOR_VAL_UINT, 0, 268435455, NULL, 0, 0, NULL },
    [CFG_CH1_THRESH_LOW] = { "ch1_thresh_low", SENSOR_VAL_UINT, 0, 268435455, NULL, 0, 0, NULL },
    [CFG_CH0_THRESH_HIGH] = { "ch0_thresh_high", SENSOR_VAL_UINT, 0, 268435455, NULL, 0, 268435455, NULL },
    [CFG_CH1_THRESH_HIGH] = { "ch1_thresh_high", SENSOR_VAL_UINT, 0, 268435455, NULL, 0, 268435455, NULL },
    [CFG_ACTIVE_CHAN] = { "active_chan", SENSOR_VAL_ENUM, 0, 0, s_active_chan_values, 2, 0, NULL },
    [CFG_AUTOSCAN] = { "autoscan", SENSOR_VAL_BOOL, 0, 1, NULL, 0, 1, NULL },
    [CFG_DEGLITCH] = { "deglitch", SENSOR_VAL_ENUM, 0, 0, s_deglitch_values, 4, 1, NULL },
    [CFG_REF_CLK_SRC] = { "ref_clk_src", SENSOR_VAL_ENUM, 0, 0, s_ref_clk_src_values, 2, 1, NULL },
    [CFG_RP_OVERRIDE] = { "rp_override", SENSOR_VAL_BOOL, 0, 1, NULL, 0, 1, NULL },
    [CFG_HIGH_CURRENT] = { "high_current", SENSOR_VAL_BOOL, 0, 1, NULL, 0, 0, NULL },
};

bool ldc_window_eval(uint8_t window, uint32_t raw, uint32_t low, uint32_t high)
{
    switch (window) {
    case 1:
        return raw < low; // BELOW
    case 2:
        return raw > high; // ABOVE
    case 3:
        return (raw < low) || (raw > high); // OUT
    case 4:
        return (raw >= low) && (raw <= high); // IN
    default:
        return false; // OFF, or not a window
    }
}

bool ldc_thresholds_ok(uint32_t low, uint32_t high)
{
    return low <= high;
}

bool ldc_present_effective(uint8_t window, bool present, bool active, bool has_sample)
{
    return window != 0 && active && has_sample && present;
}

bool ldc_any_present(const uint8_t window[2], const bool present[2], const bool active[2], const bool has_sample[2])
{
    for (int ch = 0; ch < 2; ch++) {
        if (ldc_present_effective(window[ch], present[ch], active[ch], has_sample[ch])) {
            return true;
        }
    }
    return false;
}

void ldc_unpack_data(uint16_t msb, uint16_t lsb, uint32_t *raw, uint8_t *err)
{
    *err = (uint8_t)((msb >> 12) & 0xF);
    *raw = ((uint32_t)(msb & 0x0FFF) << 16) | lsb;
}

uint32_t ldc_freq_hz(uint32_t raw, uint16_t fin_div, uint32_t f_ref)
{
    return (uint32_t)(((uint64_t)fin_div * f_ref * raw) >> 28);
}

uint32_t ldc_f_clk(uint16_t config)
{
    return (config & CONFIG_REF_CLK_SRC_BIT) ? LDC_CLKIN_HZ : LDC_INTERNAL_OSC_HZ;
}

uint32_t ldc_f_ref(uint16_t clkdiv, uint32_t f_clk)
{
    uint16_t fref_div = clkdiv & CLKDIV_FREF_DIVIDER_MASK;
    return (fref_div == 0) ? f_clk : (f_clk / fref_div);
}

uint16_t ldc_fin_div(uint16_t clkdiv)
{
    uint16_t fin_div = (uint16_t)((clkdiv & CLKDIV_FIN_DIVIDER_MASK) >> CLKDIV_FIN_DIVIDER_SHIFT);
    return (fin_div == 0) ? 1 : fin_div;
}

void ldc_active_channels(uint16_t config, uint16_t mux, bool active[2])
{
    bool autoscan = (mux & MUX_CONFIG_AUTOSCAN_EN_BIT) != 0;
    int sel = (config & CONFIG_ACTIVE_CHAN_MASK) >> CONFIG_ACTIVE_CHAN_SHIFT;
    active[0] = autoscan || (sel == 0);
    active[1] = autoscan || (sel == 1);
}

uint32_t ldc_conv_ms(uint16_t rcount, uint16_t clkdiv, uint32_t f_clk)
{
    uint32_t f_ref = ldc_f_ref(clkdiv, f_clk);
    if (f_ref == 0) {
        f_ref = 1;
    }
    // t_c = (RCOUNT * 16 + 4) / f_ref. See docs/19-sensor-ldc1612.md.
    uint64_t t_c_us = ((uint64_t)rcount * 16 + 4) * 1000000ULL / f_ref;
    return (uint32_t)((t_c_us + 999) / 1000);
}

uint32_t ldc_interval_ms(uint32_t ch0_ms, uint32_t ch1_ms, bool autoscan)
{
    uint64_t total = (uint64_t)ch0_ms + (autoscan ? ch1_ms : 0) + 5;
    if (total < LDC_POLL_MIN_MS) {
        return LDC_POLL_MIN_MS;
    }
    if (total > LDC_POLL_MAX_MS) {
        return LDC_POLL_MAX_MS;
    }
    return (uint32_t)total;
}

uint16_t ldc_field_pack(int32_t value, uint16_t mask, uint8_t shift)
{
    return (uint16_t)(((uint16_t)value << shift) & mask);
}

int32_t ldc_field_unpack(uint16_t reg, uint16_t mask, uint8_t shift)
{
    return (int32_t)((reg & mask) >> shift);
}

uint16_t ldc_field_merge(uint16_t current, uint16_t mask, uint16_t bits)
{
    return (uint16_t)((current & ~mask) | (bits & mask));
}

// MUX_CONFIG.DEGLITCH's four valid 3-bit codes, in the same order as the
// "deglitch" enum. Not a contiguous range, so the enum index cannot be
// written to the register directly.
static const uint8_t s_deglitch_codes[4] = { 0x1, 0x4, 0x5, 0x7 };

int ldc_deglitch_code(int index)
{
    if (index < 0 || index >= (int)(sizeof(s_deglitch_codes) / sizeof(s_deglitch_codes[0]))) {
        return -1;
    }
    return s_deglitch_codes[index];
}

int ldc_deglitch_index(uint16_t code)
{
    for (size_t i = 0; i < sizeof(s_deglitch_codes) / sizeof(s_deglitch_codes[0]); i++) {
        if (s_deglitch_codes[i] == code) {
            return (int)i;
        }
    }
    return -1;
}

bool ldc_autoscan_allowed(uint16_t config)
{
    return (config & CONFIG_HIGH_CURRENT_DRV_BIT) == 0;
}

bool ldc_high_current_allowed(uint16_t mux)
{
    return (mux & MUX_CONFIG_AUTOSCAN_EN_BIT) == 0;
}

bool ldc_note_poll(uint32_t *fails, bool ok)
{
    if (ok) {
        *fails = 0;
        return false;
    }
    if (*fails < UINT32_MAX) {
        (*fails)++;
    }
    return *fails >= LDC_MAX_CONSECUTIVE_POLL_FAILURES;
}

static const ldc_field_t s_fields[CFG_COUNT] = {
    [CFG_CH0_RCOUNT] = { REG_RCOUNT0, 0xFFFF, 0, LDC_WRITE_ASLEEP },
    [CFG_CH1_RCOUNT] = { REG_RCOUNT1, 0xFFFF, 0, LDC_WRITE_ASLEEP },
    [CFG_CH0_SETTLECOUNT] = { REG_SETTLECOUNT0, 0xFFFF, 0, LDC_WRITE_ASLEEP },
    [CFG_CH1_SETTLECOUNT] = { REG_SETTLECOUNT1, 0xFFFF, 0, LDC_WRITE_ASLEEP },
    [CFG_CH0_FIN_DIV] = { REG_CLOCK_DIVIDERS0, CLKDIV_FIN_DIVIDER_MASK, CLKDIV_FIN_DIVIDER_SHIFT, LDC_WRITE_ASLEEP },
    [CFG_CH1_FIN_DIV] = { REG_CLOCK_DIVIDERS1, CLKDIV_FIN_DIVIDER_MASK, CLKDIV_FIN_DIVIDER_SHIFT, LDC_WRITE_ASLEEP },
    [CFG_CH0_FREF_DIV] = { REG_CLOCK_DIVIDERS0, CLKDIV_FREF_DIVIDER_MASK, 0, LDC_WRITE_ASLEEP },
    [CFG_CH1_FREF_DIV] = { REG_CLOCK_DIVIDERS1, CLKDIV_FREF_DIVIDER_MASK, 0, LDC_WRITE_ASLEEP },
    [CFG_CH0_IDRIVE] = { REG_DRIVE_CURRENT0, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT, LDC_WRITE_ASLEEP },
    [CFG_CH1_IDRIVE] = { REG_DRIVE_CURRENT1, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT, LDC_WRITE_ASLEEP },
    [CFG_ACTIVE_CHAN] = { REG_CONFIG, CONFIG_ACTIVE_CHAN_MASK, CONFIG_ACTIVE_CHAN_SHIFT, LDC_WRITE_PLAIN },
    [CFG_AUTOSCAN] = { REG_MUX_CONFIG, MUX_CONFIG_AUTOSCAN_EN_BIT, MUX_CONFIG_AUTOSCAN_EN_SHIFT, LDC_WRITE_CHECKED },
    [CFG_REF_CLK_SRC] = { REG_CONFIG, CONFIG_REF_CLK_SRC_BIT, CONFIG_REF_CLK_SRC_SHIFT, LDC_WRITE_PLAIN },
    [CFG_RP_OVERRIDE] = { REG_CONFIG, CONFIG_RP_OVERRIDE_EN_BIT, CONFIG_RP_OVERRIDE_EN_SHIFT, LDC_WRITE_PLAIN },
    [CFG_HIGH_CURRENT] = { REG_CONFIG, CONFIG_HIGH_CURRENT_DRV_BIT, CONFIG_HIGH_CURRENT_DRV_SHIFT, LDC_WRITE_CHECKED },
};

const ldc_field_t *ldc_field_for_key(size_t key_index)
{
    if (key_index >= CFG_COUNT || s_fields[key_index].mask == 0) {
        return NULL;
    }
    return &s_fields[key_index];
}

bool ldc_held_key(size_t key_index, ldc_held_t *which, int *channel)
{
    if (key_index < CFG_CH0_WINDOW || key_index > CFG_CH1_THRESH_HIGH) {
        return false;
    }
    size_t k = key_index - CFG_CH0_WINDOW; // window pair, low pair, high pair
    *which = (ldc_held_t)(k / 2);
    *channel = (int)(k % 2);
    return true;
}
