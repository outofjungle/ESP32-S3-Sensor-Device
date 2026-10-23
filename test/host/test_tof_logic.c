// Unit tests for components/sensor_tof/src/tof_logic.c, including a
// characterisation of ST's register arithmetic (copied below as reference
// models) to prove every declared key range survives a write then a read.
#include "tinytest.h"
#include "tof_logic.h"
#include "sensor_cfg_logic.h"
#include <stdint.h>

/* ---- Reference models: ST VL53L1X_api.c, copied formula for formula. ---- */
static uint16_t st_offset_write(int16_t v)
{
    return (uint16_t)(int16_t)(v * 4);
}
static int16_t st_offset_read(uint16_t reg)
{
    uint16_t t = reg;
    t = (uint16_t)(t << 3);
    t = (uint16_t)(t >> 5);
    int16_t o = (int16_t)t;
    if (o > 1024)
        o = (int16_t)(o - 2048);
    return o;
}
static uint16_t st_xtalk_write(uint16_t v)
{
    return (uint16_t)(((uint32_t)v << 9) / 1000);
}
static uint16_t st_xtalk_read(uint16_t reg)
{
    return (uint16_t)((reg * 1000) >> 9);
}
static uint16_t st_signal_write(uint16_t v)
{
    return (uint16_t)(v >> 3);
}
static uint16_t st_signal_read(uint16_t reg)
{
    return (uint16_t)(reg << 3);
}
static bool st_sigma_write(uint16_t v, uint16_t *reg)
{
    if (v > (0xFFFF >> 2))
        return false; // ST returns an error
    *reg = (uint16_t)(v << 2);
    return true;
}
static uint16_t st_sigma_read(uint16_t reg)
{
    return (uint16_t)(reg >> 2);
}

/* ---- Bug 3: declared ranges must survive write then read ---------------- */

TEST(st_minus_1024_offset_reads_back_as_plus_1024)
{
    // The reason the declared minimum is -1023, not -1024.
    CHECK_EQ(st_offset_read(st_offset_write(-1024)), 1024);
}

TEST(offset_range_round_trips_exactly)
{
    CHECK_EQ(tof_cfgkeys[CFG_OFFSET_MM].min, -1023);
    CHECK_EQ(tof_cfgkeys[CFG_OFFSET_MM].max, 1023);
    for (int v = tof_cfgkeys[CFG_OFFSET_MM].min; v <= tof_cfgkeys[CFG_OFFSET_MM].max; v++) {
        CHECK_EQ(st_offset_read(st_offset_write((int16_t)v)), v);
    }
}

TEST(sigma_range_round_trips_and_is_accepted_by_st)
{
    CHECK_EQ(tof_cfgkeys[CFG_SIGMA_MM].max, 16383);
    uint16_t reg;
    CHECK(!st_sigma_write(16384, &reg)); // ST rejects this: why the max is 16383
    for (int v = tof_cfgkeys[CFG_SIGMA_MM].min; v <= tof_cfgkeys[CFG_SIGMA_MM].max; v++) {
        CHECK(st_sigma_write((uint16_t)v, &reg));
        CHECK_EQ(st_sigma_read(reg), v);
    }
}

TEST(signal_and_xtalk_are_lossy_in_the_chip)
{
    CHECK(st_signal_read(st_signal_write(1)) != 1); // 1 reads back as 0
    CHECK_EQ(st_signal_read(st_signal_write(65535)), 65528);
    CHECK(st_xtalk_read(st_xtalk_write(1)) != 1);
    CHECK(st_xtalk_read(st_xtalk_write(65535)) != 65535);
}

TEST(quantize_matches_the_chip_for_every_value)
{
    for (uint32_t v = 0; v <= 65535; v++) {
        CHECK_EQ(tof_signal_quantize((uint16_t)v), st_signal_read(st_signal_write((uint16_t)v)));
        CHECK_EQ(tof_xtalk_quantize((uint16_t)v), st_xtalk_read(st_xtalk_write((uint16_t)v)));
    }
}

TEST(readback_returns_request_when_chip_holds_the_rounded_value)
{
    for (int32_t v = 0; v <= 65535; v += 97) {
        int32_t q = tof_xtalk_quantize((uint16_t)v);
        CHECK_EQ(tof_readback(true, v, q, q), v); // verify would pass
    }
}

TEST(readback_reports_the_chip_value_when_it_changed_underneath)
{
    // A CAL xtalk run changed the chip: do not hide it behind the old request.
    int32_t q = tof_xtalk_quantize(1000);
    CHECK_EQ(tof_readback(true, 1000, q + 2, q), q + 2);
    CHECK_EQ(tof_readback(false, 0, 77, 0), 77); // never written
}

/* ---- timing ----------------------------------------------------------- */

TEST(budget_allowed_table)
{
    const uint16_t ok[] = { 20, 33, 50, 100, 200, 500 };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        CHECK(tof_budget_allowed(ok[i], 1));
        CHECK(tof_budget_allowed(ok[i], 2));
    }
    CHECK(tof_budget_allowed(15, 1)); // SHORT only
    CHECK(!tof_budget_allowed(15, 2));
    const uint16_t bad[] = { 0, 1, 14, 16, 19, 21, 34, 499, 501, 65535 };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!tof_budget_allowed(bad[i], 1));
        CHECK(!tof_budget_allowed(bad[i], 2));
    }
}

TEST(budget_for_mode_fixup)
{
    CHECK_EQ(tof_budget_for_mode(15, 2), 20);
    CHECK_EQ(tof_budget_for_mode(15, 1), 15);
    CHECK_EQ(tof_budget_for_mode(50, 2), 50);
    CHECK_EQ(tof_budget_for_mode(50, 1), 50);
}

TEST(intermeasurement_must_cover_budget)
{
    CHECK(tof_intermeas_ok(50, 50));
    CHECK(!tof_intermeas_ok(49, 50));
    CHECK(tof_intermeas_ok(5000, 500));
    CHECK(!tof_intermeas_ok(0, 15));
}

TEST(poll_interval_never_stalls)
{
    CHECK_EQ(tof_poll_interval_ms(100, true), 105u);
    CHECK_EQ(tof_poll_interval_ms(20, true), 25u);
    CHECK_EQ(tof_poll_interval_ms(5000, true), 5005u);
    // A failed read, or a nonsense value (a zero divisor right after boot
    // can give ~65 s), falls back to the 100 ms default.
    CHECK_EQ(tof_poll_interval_ms(0, true), 105u);
    CHECK_EQ(tof_poll_interval_ms(65535, true), 105u);
    CHECK_EQ(tof_poll_interval_ms(2000, false), 105u);
}

/* ---- enum mappings ---------------------------------------------------- */

TEST(enum_mappings_round_trip)
{
    CHECK_EQ(tof_uld_dist_mode(0), 1);
    CHECK_EQ(tof_uld_dist_mode(1), 2);
    CHECK_EQ(tof_dist_mode_index(1), 0);
    CHECK_EQ(tof_dist_mode_index(2), 1);
    CHECK_EQ(tof_dist_mode_index(0), 1); // an unknown ST mode reads as LONG
    CHECK_EQ(tof_dist_mode_index(3), 1);
    CHECK_EQ(tof_uld_polarity(0), 1);
    CHECK_EQ(tof_uld_polarity(1), 0);
    CHECK_EQ(tof_polarity_index(1), 0);
    CHECK_EQ(tof_polarity_index(0), 1);
}

/* ---- window and thresholds -------------------------------------------- */

TEST(window_register_decode)
{
    CHECK(tof_window_is_off(0x20));
    CHECK_EQ(tof_window_decode(0x20), 0);
    CHECK_EQ(tof_window_decode(0x24), 0); // bit 0x20 wins over the low bits
    CHECK_EQ(tof_window_decode(0x23), 0);
    CHECK_EQ(tof_window_decode(0x00), 1); // BELOW: low bits 0, bit 0x20 clear
    CHECK_EQ(tof_window_decode(0x01), 2);
    CHECK_EQ(tof_window_decode(0x02), 3);
    CHECK_EQ(tof_window_decode(0x03), 4);
    CHECK(!tof_window_is_off(0x03));
}

TEST(window_mode_round_trips)
{
    for (int32_t idx = 1; idx <= 4; idx++) {
        CHECK_EQ(tof_window_decode(tof_window_mode(idx)), idx);
    }
}

TEST(thresholds)
{
    CHECK(tof_thresh_ok(50, 1500));
    CHECK(tof_thresh_ok(50, 50));
    CHECK(!tof_thresh_ok(51, 50));
    CHECK(tof_thresh_ok(0, 0));
}

/* ---- calibration ------------------------------------------------------ */

TEST(cal_target_bounds)
{
    CHECK_EQ(tof_cal_validate("offset", 9), TOF_CAL_BAD_ARG);
    CHECK_EQ(tof_cal_validate("offset", 10), TOF_CAL_OFFSET);
    CHECK_EQ(tof_cal_validate("offset", 600), TOF_CAL_OFFSET);
    CHECK_EQ(tof_cal_validate("offset", 601), TOF_CAL_BAD_ARG);
    CHECK_EQ(tof_cal_validate("offset", -1), TOF_CAL_BAD_ARG);
    CHECK_EQ(tof_cal_validate("xtalk", 0), TOF_CAL_BAD_ARG); // would divide by zero
    CHECK_EQ(tof_cal_validate("xtalk", 1), TOF_CAL_XTALK);
    CHECK_EQ(tof_cal_validate("xtalk", 4000), TOF_CAL_XTALK);
    CHECK_EQ(tof_cal_validate("xtalk", 4001), TOF_CAL_BAD_ARG);
    CHECK_EQ(tof_cal_validate("xtalk", INT32_MIN), TOF_CAL_BAD_ARG);
}

TEST(cal_unknown_action)
{
    CHECK_EQ(tof_cal_validate("nosuch", 100), TOF_CAL_UNKNOWN);
    CHECK_EQ(tof_cal_validate("", 100), TOF_CAL_UNKNOWN);
    CHECK_EQ(tof_cal_validate("Offset", 100), TOF_CAL_UNKNOWN);
    CHECK_EQ(tof_cal_validate(NULL, 100), TOF_CAL_UNKNOWN);
}

/* ---- the key table ---------------------------------------------------- */

TEST(key_table_is_consistent)
{
    CHECK_EQ(CFG_COUNT, 14);
    for (int i = 0; i < CFG_COUNT; i++) {
        CHECK(scl_cfgkey_def_valid(&tof_cfgkeys[i]));
        for (int j = i + 1; j < CFG_COUNT; j++) {
            CHECK(strcmp(tof_cfgkeys[i].name, tof_cfgkeys[j].name) != 0);
        }
    }
    CHECK_STREQ(tof_cfgkeys[CFG_XTALK_CPS].unit, "cps");
}

TEST(default_budget_and_intermeasurement_are_legal_together)
{
    int32_t budget = tof_cfgkeys[CFG_BUDGET_MS].default_value;
    int32_t im = tof_cfgkeys[CFG_INTERMEAS_MS].default_value;
    int32_t mode = tof_cfgkeys[CFG_DIST_MODE].default_value;
    CHECK(tof_budget_allowed((uint16_t)budget, tof_uld_dist_mode(mode)));
    CHECK(tof_intermeas_ok((uint32_t)im, (uint16_t)budget));
    CHECK(tof_thresh_ok((uint16_t)tof_cfgkeys[CFG_THRESH_LOW_MM].default_value,
                        (uint16_t)tof_cfgkeys[CFG_THRESH_HIGH_MM].default_value));
}

int main(void)
{
    RUN_TEST(st_minus_1024_offset_reads_back_as_plus_1024);
    RUN_TEST(offset_range_round_trips_exactly);
    RUN_TEST(sigma_range_round_trips_and_is_accepted_by_st);
    RUN_TEST(signal_and_xtalk_are_lossy_in_the_chip);
    RUN_TEST(quantize_matches_the_chip_for_every_value);
    RUN_TEST(readback_returns_request_when_chip_holds_the_rounded_value);
    RUN_TEST(readback_reports_the_chip_value_when_it_changed_underneath);
    RUN_TEST(budget_allowed_table);
    RUN_TEST(budget_for_mode_fixup);
    RUN_TEST(intermeasurement_must_cover_budget);
    RUN_TEST(poll_interval_never_stalls);
    RUN_TEST(enum_mappings_round_trip);
    RUN_TEST(window_register_decode);
    RUN_TEST(window_mode_round_trips);
    RUN_TEST(thresholds);
    RUN_TEST(cal_target_bounds);
    RUN_TEST(cal_unknown_action);
    RUN_TEST(key_table_is_consistent);
    RUN_TEST(default_budget_and_intermeasurement_are_legal_together);
    return tinytest_summary();
}
