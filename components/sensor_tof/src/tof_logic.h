#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sensor_cfgkey.h"

// Pure VL53L1X logic split out of sensor_tof.c so it runs in the host-side
// unit tests (test/host/test_tof_logic.c). No ESP-IDF, FreeRTOS, or I2C.
// "uld mode" below is ST's encoding: 1 = SHORT, 2 = LONG.

// ---- configuration keys -------------------------------------------------

enum {
    CFG_DIST_MODE,
    CFG_BUDGET_MS,
    CFG_INTERMEAS_MS,
    CFG_ROI_X,
    CFG_ROI_Y,
    CFG_ROI_CENTER,
    CFG_OFFSET_MM,
    CFG_XTALK_CPS,
    CFG_SIGMA_MM,
    CFG_SIGNAL_KCPS,
    CFG_WINDOW,
    CFG_THRESH_LOW_MM,
    CFG_THRESH_HIGH_MM,
    CFG_INT_POLARITY,
    CFG_COUNT,
};

extern const sensor_cfgkey_def_t tof_cfgkeys[CFG_COUNT];

// ---- timing ---------------------------------------------------------------

// 15 ms is valid in SHORT mode only; the other listed budgets in either mode.
bool tof_budget_allowed(uint16_t budget_ms, uint16_t uld_mode);

// Keeps a budget legal after a mode change: 15 ms in LONG mode becomes 20 ms.
uint16_t tof_budget_for_mode(uint16_t budget_ms, uint16_t uld_mode);

// The inter-measurement period must be at least the timing budget.
bool tof_intermeas_ok(uint32_t intermeas_ms, uint16_t budget_ms);

// The poll interval: the chip's inter-measurement period plus a margin. If
// the read failed, or returned a value outside the legal range, the default
// period is used instead of a value that could stall polling.
uint32_t tof_poll_interval_ms(uint16_t intermeas_ms, bool read_ok);

// ---- enum mappings ------------------------------------------------------

uint16_t tof_uld_dist_mode(int32_t index);      // 0 SHORT -> 1, otherwise LONG -> 2
int32_t tof_dist_mode_index(uint16_t uld_mode); // 1 -> 0, anything else -> 1
uint8_t tof_uld_polarity(int32_t index);        // 0 HIGH -> 1, otherwise 0
int32_t tof_polarity_index(uint8_t uld_polarity);

// ---- window and thresholds ------------------------------------------------

// SYSTEM__INTERRUPT_CONFIG_GPIO: bit 0x20 set means window detection is OFF
// (the chip's reset value). Otherwise the low 2 bits are the window mode.
bool tof_window_is_off(uint8_t reg);
int32_t tof_window_decode(uint8_t reg);        // 0 OFF, 1..4 BELOW..IN
uint8_t tof_window_mode(int32_t window_index); // index 1..4 -> ST's 0..3

bool tof_thresh_ok(uint16_t low, uint16_t high);

// ---- values the chip stores in a reduced form ---------------------------

// ST stores crosstalk as (cps << 9) / 1000 and signal as kcps >> 3, so the
// chip returns a rounded value. These give the value the chip will hold
// after a write, so a read-back can be matched against what was requested.
uint16_t tof_xtalk_quantize(uint16_t cps);
uint16_t tof_signal_quantize(uint16_t kcps);

// Returns requested if the chip holds exactly what writing it would give,
// otherwise the chip's own value (it was changed some other way).
int32_t tof_readback(bool have_request, int32_t requested, int32_t chip, int32_t quantized_request);

// ---- calibration ----------------------------------------------------------

#define TOF_CAL_OFFSET_TARGET_MIN_MM 10
#define TOF_CAL_OFFSET_TARGET_MAX_MM 600
#define TOF_CAL_XTALK_TARGET_MIN_MM  1
#define TOF_CAL_XTALK_TARGET_MAX_MM  4000

typedef enum {
    TOF_CAL_OFFSET,
    TOF_CAL_XTALK,
    TOF_CAL_UNKNOWN, // not a known action name
    TOF_CAL_BAD_ARG, // known action, target outside its safe range
} tof_cal_kind_t;

// Classifies a CAL request. Outside the safe range the vendored routines hit
// undefined behavior (an int16_t accumulator overflow, a division by zero).
tof_cal_kind_t tof_cal_validate(const char *action_name, int32_t target_mm);
