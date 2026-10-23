#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sensor_cfgkey.h"

// Pure LDC1612 logic split out of sensor_ldc.c so it runs in the host-side
// unit tests (test/host/test_ldc_logic.c). No ESP-IDF, FreeRTOS, or I2C.
// See docs/19-sensor-ldc1612.md for the formulas.

// This firmware's two reference clock choices, matching the "ref_clk_src"
// configuration key. 40 MHz is the Grove unit's own CLKIN oscillator; 43
// MHz is the chip's internal oscillator's typical frequency (the
// datasheet gives no tighter figure). See docs/19-sensor-ldc1612.md.
#define LDC_CLKIN_HZ        40000000u
#define LDC_INTERNAL_OSC_HZ 43000000u

// A poll interval is never shorter than this, and never longer than the
// maximum: two in-range settings (a large RCOUNT with a large FREF_DIVIDER)
// would otherwise freeze the channel for almost a minute with no error.
#define LDC_POLL_MIN_MS 10u
#define LDC_POLL_MAX_MS 1000u

// After this many consecutive poll() failures the instance is faulted.
#define LDC_MAX_CONSECUTIVE_POLL_FAILURES 5

// ---- configuration keys ------------------------------------------------

enum {
    CFG_CH0_RCOUNT,
    CFG_CH1_RCOUNT,
    CFG_CH0_SETTLECOUNT,
    CFG_CH1_SETTLECOUNT,
    CFG_CH0_FIN_DIV,
    CFG_CH1_FIN_DIV,
    CFG_CH0_FREF_DIV,
    CFG_CH1_FREF_DIV,
    CFG_CH0_IDRIVE,
    CFG_CH1_IDRIVE,
    CFG_CH0_WINDOW,
    CFG_CH1_WINDOW,
    CFG_CH0_THRESH_LOW,
    CFG_CH1_THRESH_LOW,
    CFG_CH0_THRESH_HIGH,
    CFG_CH1_THRESH_HIGH,
    CFG_ACTIVE_CHAN,
    CFG_AUTOSCAN,
    CFG_DEGLITCH,
    CFG_REF_CLK_SRC,
    CFG_RP_OVERRIDE,
    CFG_HIGH_CURRENT,
    CFG_COUNT,
};

extern const sensor_cfgkey_def_t ldc_cfgkeys[CFG_COUNT];

// ---- how each key maps to the chip ---------------------------------------

typedef enum {
    LDC_WRITE_PLAIN,   // read-modify-write of the field
    LDC_WRITE_ASLEEP,  // the same, inside a sleep bracket (the chip must not convert)
    LDC_WRITE_CHECKED, // a flag that excludes another one; the driver checks first
} ldc_write_t;

typedef struct {
    uint8_t reg;
    uint16_t mask;
    uint8_t shift;
    ldc_write_t write;
} ldc_field_t;

// The register field behind a key, or NULL for a key with none: the six
// firmware-held keys and deglitch (whose codes are not contiguous).
const ldc_field_t *ldc_field_for_key(size_t key_index);

typedef enum { LDC_HELD_WINDOW, LDC_HELD_THRESH_LOW, LDC_HELD_THRESH_HIGH } ldc_held_t;

// For a firmware-held key, which value it is and for which channel (0 or 1).
bool ldc_held_key(size_t key_index, ldc_held_t *which, int *channel);

// ---- window and threshold -----------------------------------------------

// Window index 0 is OFF; see the "ch0_window" enum. BELOW and ABOVE are
// strict; IN includes both thresholds. Any other index gives false.
bool ldc_window_eval(uint8_t window, uint32_t raw, uint32_t low, uint32_t high);

// A low threshold above the high one makes IN impossible and OUT always true.
bool ldc_thresholds_ok(uint32_t low, uint32_t high);

// The value a presence channel reports: window must be on, the channel must
// be converting, and the sample must be fresh. Otherwise false, never a
// stale true from an earlier cycle.
bool ldc_present_effective(uint8_t window, bool present, bool active, bool has_sample);

// True if any channel reports presence (see ldc_present_effective).
bool ldc_any_present(const uint8_t window[2], const bool present[2], const bool active[2], const bool has_sample[2]);

// ---- data and clocks ----------------------------------------------------

// Splits DATAx_MSB and DATAx_LSB: *err is the 4 error bits, *raw the 28-bit count.
void ldc_unpack_data(uint16_t msb, uint16_t lsb, uint32_t *raw, uint8_t *err);

// f_sensor = fin_div * f_ref * raw / 2^28.
uint32_t ldc_freq_hz(uint32_t raw, uint16_t fin_div, uint32_t f_ref);

uint32_t ldc_f_clk(uint16_t config);                 // CONFIG.REF_CLK_SRC
uint32_t ldc_f_ref(uint16_t clkdiv, uint32_t f_clk); // fref_div 0 means f_clk
uint16_t ldc_fin_div(uint16_t clkdiv);               // 0 is treated as 1
void ldc_active_channels(uint16_t config, uint16_t mux, bool active[2]);

// Conversion time of one channel, rounded up to whole milliseconds.
uint32_t ldc_conv_ms(uint16_t rcount, uint16_t clkdiv, uint32_t f_clk);

// The poll interval for the given conversion times: both channels when
// autoscan is on, plus a margin, clamped to [LDC_POLL_MIN_MS, LDC_POLL_MAX_MS].
uint32_t ldc_interval_ms(uint32_t ch0_ms, uint32_t ch1_ms, bool autoscan);

// ---- register fields ----------------------------------------------------

uint16_t ldc_field_pack(int32_t value, uint16_t mask, uint8_t shift);
int32_t ldc_field_unpack(uint16_t reg, uint16_t mask, uint8_t shift);
uint16_t ldc_field_merge(uint16_t current, uint16_t mask, uint16_t bits);

// MUX_CONFIG.DEGLITCH codes, by enum index. -1 for an invalid index or code.
int ldc_deglitch_code(int index);
int ldc_deglitch_index(uint16_t code);

// ---- rules between keys ----------------------------------------------

bool ldc_autoscan_allowed(uint16_t config);  // false while HIGH_CURRENT is set
bool ldc_high_current_allowed(uint16_t mux); // false while AUTOSCAN is set

// Records one poll result. Returns true once the failure count reaches
// LDC_MAX_CONSECUTIVE_POLL_FAILURES. A success resets the count.
bool ldc_note_poll(uint32_t *fails, bool ok);
