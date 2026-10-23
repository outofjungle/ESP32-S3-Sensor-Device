#include "tof_logic.h"
#include <string.h>

static const char *const s_dist_mode_values[] = { "SHORT", "LONG" };
static const char *const s_window_values[] = { "OFF", "BELOW", "ABOVE", "OUT", "IN" };
static const char *const s_int_polarity_values[] = { "HIGH", "LOW" };

// Ranges are the ones a write followed by a read-back returns exactly:
//  - offset_mm: ST stores it in 11 bits; -1024 reads back as +1024.
//  - sigma_mm: ST refuses a value above 16383 (it is stored shifted by 2).
// xtalk_cps and signal_kcps are stored in a reduced form; see tof_readback().
const sensor_cfgkey_def_t tof_cfgkeys[CFG_COUNT] = {
    [CFG_DIST_MODE] = { "dist_mode", SENSOR_VAL_ENUM, 0, 0, s_dist_mode_values, 2, 1, NULL },
    [CFG_BUDGET_MS] = { "budget_ms", SENSOR_VAL_UINT, 15, 500, NULL, 0, 50, "ms" },
    [CFG_INTERMEAS_MS] = { "intermeas_ms", SENSOR_VAL_UINT, 20, 5000, NULL, 0, 100, "ms" },
    [CFG_ROI_X] = { "roi_x", SENSOR_VAL_UINT, 4, 16, NULL, 0, 16, NULL },
    [CFG_ROI_Y] = { "roi_y", SENSOR_VAL_UINT, 4, 16, NULL, 0, 16, NULL },
    [CFG_ROI_CENTER] = { "roi_center", SENSOR_VAL_UINT, 0, 255, NULL, 0, 199, NULL },
    [CFG_OFFSET_MM] = { "offset_mm", SENSOR_VAL_INT, -1023, 1023, NULL, 0, 0, "mm" },
    [CFG_XTALK_CPS] = { "xtalk_cps", SENSOR_VAL_UINT, 0, 65535, NULL, 0, 0, "cps" },
    [CFG_SIGMA_MM] = { "sigma_mm", SENSOR_VAL_UINT, 1, 16383, NULL, 0, 15, "mm" },
    [CFG_SIGNAL_KCPS] = { "signal_kcps", SENSOR_VAL_UINT, 1, 65535, NULL, 0, 1024, "kcps" },
    [CFG_WINDOW] = { "window", SENSOR_VAL_ENUM, 0, 0, s_window_values, 5, 4, NULL },
    [CFG_THRESH_LOW_MM] = { "thresh_low_mm", SENSOR_VAL_UINT, 0, 4000, NULL, 0, 50, "mm" },
    [CFG_THRESH_HIGH_MM] = { "thresh_high_mm", SENSOR_VAL_UINT, 0, 4000, NULL, 0, 1500, "mm" },
    [CFG_INT_POLARITY] = { "int_polarity", SENSOR_VAL_ENUM, 0, 0, s_int_polarity_values, 2, 0, NULL },
};

// The allowed VL53L1X_SetTimingBudgetInMs() values.
static const uint16_t s_allowed_budgets_ms[] = { 15, 20, 33, 50, 100, 200, 500 };

bool tof_budget_allowed(uint16_t budget_ms, uint16_t uld_mode)
{
    for (size_t i = 0; i < sizeof(s_allowed_budgets_ms) / sizeof(s_allowed_budgets_ms[0]); i++) {
        if (s_allowed_budgets_ms[i] == budget_ms) {
            return budget_ms != 15 || uld_mode == 1;
        }
    }
    return false;
}

uint16_t tof_budget_for_mode(uint16_t budget_ms, uint16_t uld_mode)
{
    return (uld_mode == 2 && budget_ms == 15) ? 20 : budget_ms;
}

bool tof_intermeas_ok(uint32_t intermeas_ms, uint16_t budget_ms)
{
    return intermeas_ms >= budget_ms;
}

uint32_t tof_poll_interval_ms(uint16_t intermeas_ms, bool read_ok)
{
    uint32_t im = read_ok ? intermeas_ms : 100;
    if (im < 20 || im > 5000) {
        im = 100;
    }
    return im + 5; // a little margin over the chip's own cadence
}

uint16_t tof_uld_dist_mode(int32_t index)
{
    return (index == 0) ? 1 : 2;
}

int32_t tof_dist_mode_index(uint16_t uld_mode)
{
    return (uld_mode == 1) ? 0 : 1;
}

uint8_t tof_uld_polarity(int32_t index)
{
    return (index == 0) ? 1 : 0;
}

int32_t tof_polarity_index(uint8_t uld_polarity)
{
    return (uld_polarity == 1) ? 0 : 1;
}

bool tof_window_is_off(uint8_t reg)
{
    return (reg & 0x20) != 0;
}

int32_t tof_window_decode(uint8_t reg)
{
    return tof_window_is_off(reg) ? 0 : ((reg & 0x3) + 1);
}

uint8_t tof_window_mode(int32_t window_index)
{
    return (uint8_t)(window_index - 1);
}

bool tof_thresh_ok(uint16_t low, uint16_t high)
{
    return low <= high;
}

uint16_t tof_xtalk_quantize(uint16_t cps)
{
    uint16_t reg = (uint16_t)(((uint32_t)cps << 9) / 1000);
    return (uint16_t)(((uint32_t)reg * 1000) >> 9);
}

uint16_t tof_signal_quantize(uint16_t kcps)
{
    return (uint16_t)((kcps >> 3) << 3);
}

int32_t tof_readback(bool have_request, int32_t requested, int32_t chip, int32_t quantized_request)
{
    return (have_request && chip == quantized_request) ? requested : chip;
}

tof_cal_kind_t tof_cal_validate(const char *action_name, int32_t target_mm)
{
    if (action_name == NULL) {
        return TOF_CAL_UNKNOWN;
    }
    if (strcmp(action_name, "offset") == 0) {
        return (target_mm >= TOF_CAL_OFFSET_TARGET_MIN_MM && target_mm <= TOF_CAL_OFFSET_TARGET_MAX_MM)
                   ? TOF_CAL_OFFSET
                   : TOF_CAL_BAD_ARG;
    }
    if (strcmp(action_name, "xtalk") == 0) {
        return (target_mm >= TOF_CAL_XTALK_TARGET_MIN_MM && target_mm <= TOF_CAL_XTALK_TARGET_MAX_MM) ? TOF_CAL_XTALK
                                                                                                      : TOF_CAL_BAD_ARG;
    }
    return TOF_CAL_UNKNOWN;
}
