// Unit tests for components/sensor_ldc/src/ldc_logic.c.
#include "tinytest.h"
#include "ldc_logic.h"
#include "ldc_regs.h"
#include "sensor_cfg_logic.h"
#include <stdint.h>

/* ---- window evaluation --------------------------------------------- */

TEST(window_modes_and_boundaries)
{
    // BELOW (1) and ABOVE (2) are strict.
    CHECK(ldc_window_eval(1, 99, 100, 200));
    CHECK(!ldc_window_eval(1, 100, 100, 200));
    CHECK(ldc_window_eval(2, 201, 100, 200));
    CHECK(!ldc_window_eval(2, 200, 100, 200));
    // OUT (3) is the complement of IN (4) inclusive.
    CHECK(ldc_window_eval(3, 99, 100, 200) && ldc_window_eval(3, 201, 100, 200));
    CHECK(!ldc_window_eval(3, 100, 100, 200) && !ldc_window_eval(3, 200, 100, 200));
    CHECK(ldc_window_eval(4, 100, 100, 200) && ldc_window_eval(4, 200, 100, 200));
    CHECK(!ldc_window_eval(4, 99, 100, 200) && !ldc_window_eval(4, 201, 100, 200));
}

TEST(window_off_and_invalid_are_false)
{
    CHECK(!ldc_window_eval(0, 150, 100, 200));
    for (int w = 5; w < 256; w += 25)
        CHECK(!ldc_window_eval((uint8_t)w, 150, 100, 200));
}

TEST(window_with_low_equal_high_and_inverted)
{
    CHECK(ldc_window_eval(4, 50, 50, 50));
    CHECK(!ldc_window_eval(4, 50, 100, 10)); // inverted: IN never true
    CHECK(ldc_window_eval(3, 50, 100, 10));  // inverted: OUT always true
    CHECK(ldc_window_eval(3, 200, 100, 10));
}

TEST(thresholds_ok)
{
    CHECK(ldc_thresholds_ok(0, 0));
    CHECK(ldc_thresholds_ok(10, 11));
    CHECK(!ldc_thresholds_ok(11, 10));
    CHECK(ldc_thresholds_ok(0, 268435455));
}

TEST(present_is_never_stale)
{
    // Bug 4: a stale present=true must not be reported once the channel
    // stops converting or the last poll failed.
    CHECK(ldc_present_effective(4, true, true, true));
    CHECK(!ldc_present_effective(0, true, true, true));  // window OFF
    CHECK(!ldc_present_effective(4, true, false, true)); // not converting
    CHECK(!ldc_present_effective(4, true, true, false)); // no fresh sample
    CHECK(!ldc_present_effective(4, false, true, true));
}

TEST(any_present_combines_channels)
{
    uint8_t w[2] = { 4, 4 };
    bool p[2] = { false, true }, a[2] = { true, true }, h[2] = { true, true };
    CHECK(ldc_any_present(w, p, a, h));
    a[1] = false; // autoscan off, ch1 idle
    CHECK(!ldc_any_present(w, p, a, h));
    a[1] = true;
    h[1] = false; // poll failed
    CHECK(!ldc_any_present(w, p, a, h));
    h[1] = true;
    w[1] = 0; // window OFF
    CHECK(!ldc_any_present(w, p, a, h));
}

/* ---- data and clocks ----------------------------------------------- */

TEST(unpack_data)
{
    uint32_t raw;
    uint8_t err;
    ldc_unpack_data(0xFFFF, 0xFFFF, &raw, &err);
    CHECK_EQ(err, 0xF);
    CHECK_EQ(raw, 0x0FFFFFFFu);
    ldc_unpack_data(0x0000, 0x0000, &raw, &err);
    CHECK(raw == 0 && err == 0);
    ldc_unpack_data(0x3ABC, 0x1234, &raw, &err);
    CHECK_EQ(err, 0x3);
    CHECK_EQ(raw, 0x0ABC1234u); // error bits never leak into the count
}

TEST(freq_math)
{
    CHECK_EQ(ldc_freq_hz(0, 1, 40000000u), 0u);
    // raw = 2^28 / fin_div -> exactly f_ref
    CHECK_EQ(ldc_freq_hz(1u << 27, 2, 20000000u), 20000000u);
    // Largest inputs must not overflow 64-bit math and must fit in 32 bits.
    uint32_t f = ldc_freq_hz(0x0FFFFFFFu, 15, 43000000u);
    CHECK(f > 0 && f <= 15u * 43000000u);
    CHECK_EQ(ldc_freq_hz(1, 1, 40000000u), 0u); // truncates toward zero
}

TEST(clock_decode)
{
    CHECK_EQ(ldc_f_clk(CONFIG_REF_CLK_SRC_BIT), LDC_CLKIN_HZ);
    CHECK_EQ(ldc_f_clk(0), LDC_INTERNAL_OSC_HZ);
    CHECK_EQ(ldc_f_ref(0x0000, 40000000u), 40000000u); // fref_div 0 -> f_clk
    CHECK_EQ(ldc_f_ref(0x0002, 40000000u), 20000000u);
    CHECK_EQ(ldc_f_ref(0x03FF, 40000000u), 40000000u / 1023u);
    CHECK_EQ(ldc_f_ref(0xF002, 40000000u), 20000000u); // fin_div bits ignored
    CHECK_EQ(ldc_fin_div(0x0000), 1);                  // 0 treated as 1
    CHECK_EQ(ldc_fin_div(0xF000), 15);
    CHECK_EQ(ldc_fin_div(0x1000), 1);
}

TEST(active_channels)
{
    bool a[2];
    ldc_active_channels(0, MUX_CONFIG_AUTOSCAN_EN_BIT, a);
    CHECK(a[0] && a[1]);
    ldc_active_channels(0u << CONFIG_ACTIVE_CHAN_SHIFT, 0, a);
    CHECK(a[0] && !a[1]);
    ldc_active_channels(1u << CONFIG_ACTIVE_CHAN_SHIFT, 0, a);
    CHECK(!a[0] && a[1]);
    // The chip's field is 2 bits wide; 2 and 3 select neither channel.
    ldc_active_channels(2u << CONFIG_ACTIVE_CHAN_SHIFT, 0, a);
    CHECK(!a[0] && !a[1]);
    ldc_active_channels(3u << CONFIG_ACTIVE_CHAN_SHIFT, 0, a);
    CHECK(!a[0] && !a[1]);
}

TEST(conversion_time)
{
    // rcount 1350, fref_div 2, 40 MHz: (1350*16+4)/20e6 = 1.0802 ms -> 2 ms
    CHECK_EQ(ldc_conv_ms(1350, 0x0002, 40000000u), 2u);
    CHECK_EQ(ldc_conv_ms(4, 0x0002, 40000000u), 1u);      // smallest legal RCOUNT rounds up to 1 ms
    uint32_t big = ldc_conv_ms(65535, 0x03FF, 40000000u); // the worst case
    CHECK(big > 20000);                                   // tens of seconds
}

TEST(conversion_time_guards_a_zero_reference_clock)
{
    // f_ref would be 0 (100 / 1023); the guard makes it 1 Hz rather than divide by zero.
    uint32_t ms = ldc_conv_ms(100, 0x03FF, 100);
    CHECK(ms > 1000); // (100*16+4) cycles at 1 Hz is 1604 s, in ms
}

TEST(interval_is_clamped)
{
    CHECK_EQ(ldc_interval_ms(0, 0, false), LDC_POLL_MIN_MS);
    CHECK_EQ(ldc_interval_ms(2, 2, true), 10u);
    CHECK_EQ(ldc_interval_ms(2, 2, false), 10u); // 2+5=7 -> minimum 10
    CHECK_EQ(ldc_interval_ms(20, 30, false), 25u);
    CHECK_EQ(ldc_interval_ms(20, 30, true), 55u);
    CHECK_EQ(ldc_interval_ms(54000, 54000, true), LDC_POLL_MAX_MS); // no 54 s freeze
    CHECK_EQ(ldc_interval_ms(UINT32_MAX, UINT32_MAX, true), LDC_POLL_MAX_MS);
}

/* ---- register fields ----------------------------------------------- */

TEST(field_pack_unpack_merge)
{
    CHECK_EQ(ldc_field_pack(15, CLKDIV_FIN_DIVIDER_MASK, CLKDIV_FIN_DIVIDER_SHIFT), 0xF000);
    CHECK_EQ(ldc_field_pack(31, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT), 0xF800);
    // A value wider than the field is cut to the mask.
    CHECK_EQ(ldc_field_pack(0x1F, CLKDIV_FIN_DIVIDER_MASK, CLKDIV_FIN_DIVIDER_SHIFT), 0xF000);
    CHECK_EQ(ldc_field_unpack(0xF123, CLKDIV_FIN_DIVIDER_MASK, CLKDIV_FIN_DIVIDER_SHIFT), 15);
    CHECK_EQ(ldc_field_unpack(0xA000, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT), 20);
    // Merge keeps the bits outside the mask.
    CHECK_EQ(ldc_field_merge(0x0FFF, 0xF000, 0xA000), 0xAFFF);
    CHECK_EQ(ldc_field_merge(0xFFFF, 0x0007, 0x0001), 0xFFF9);
    CHECK_EQ(ldc_field_merge(0x1234, 0x0000, 0xFFFF), 0x1234);
}

TEST(field_round_trip_for_every_idrive)
{
    for (int v = 0; v <= 31; v++) {
        uint16_t reg = ldc_field_merge(0x07FF, DRIVE_CURRENT_IDRIVE_MASK,
                                       ldc_field_pack(v, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT));
        CHECK_EQ(ldc_field_unpack(reg, DRIVE_CURRENT_IDRIVE_MASK, DRIVE_CURRENT_IDRIVE_SHIFT), v);
        CHECK_EQ(reg & 0x07FF, 0x07FF);
    }
}

TEST(deglitch_codes_round_trip)
{
    const int want[4] = { 0x1, 0x4, 0x5, 0x7 };
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(ldc_deglitch_code(i), want[i]);
        CHECK_EQ(ldc_deglitch_index((uint16_t)want[i]), i);
    }
    CHECK_EQ(ldc_deglitch_code(-1), -1);
    CHECK_EQ(ldc_deglitch_code(4), -1);
    int bad[] = { 0, 2, 3, 6, 8, 0xFFFF };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(ldc_deglitch_index((uint16_t)bad[i]), -1);
    }
}

TEST(mutual_exclusion_rules)
{
    CHECK(ldc_autoscan_allowed(0));
    CHECK(!ldc_autoscan_allowed(CONFIG_HIGH_CURRENT_DRV_BIT));
    CHECK(ldc_autoscan_allowed((uint16_t)~CONFIG_HIGH_CURRENT_DRV_BIT));
    CHECK(ldc_high_current_allowed(0));
    CHECK(!ldc_high_current_allowed(MUX_CONFIG_AUTOSCAN_EN_BIT));
}

TEST(poll_failure_counter)
{
    uint32_t fails = 0;
    for (int i = 1; i < LDC_MAX_CONSECUTIVE_POLL_FAILURES; i++) {
        CHECK(!ldc_note_poll(&fails, false)); // 1..4: not yet
    }
    CHECK(ldc_note_poll(&fails, false)); // the 5th
    CHECK(ldc_note_poll(&fails, false)); // stays true
    CHECK(!ldc_note_poll(&fails, true)); // success resets
    CHECK_EQ(fails, 0u);
    CHECK(!ldc_note_poll(&fails, false));
    fails = UINT32_MAX; // no wrap back to 0
    CHECK(ldc_note_poll(&fails, false));
    CHECK_EQ(fails, UINT32_MAX);
}

/* ---- the key table ------------------------------------------------- */

TEST(key_table_is_consistent)
{
    CHECK_EQ(CFG_COUNT, 22);
    CHECK(CFG_COUNT <= 24); // SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER
    for (int i = 0; i < CFG_COUNT; i++) {
        CHECK(scl_cfgkey_def_valid(&ldc_cfgkeys[i]));
    }
    CHECK_STREQ(ldc_cfgkeys[CFG_CH0_RCOUNT].name, "ch0_rcount");
    CHECK_STREQ(ldc_cfgkeys[CFG_HIGH_CURRENT].name, "high_current");
    CHECK_EQ(ldc_cfgkeys[CFG_DEGLITCH].enum_count, 4u);
    CHECK_EQ(ldc_cfgkeys[CFG_CH0_THRESH_HIGH].default_value, 268435455);
}

TEST(field_table_matches_the_register_layout)
{
    // Every key with a register field maps to a distinct (register, mask) pair
    // unless it shares a register on purpose (the two clock dividers, CONFIG).
    for (size_t i = 0; i < CFG_COUNT; i++) {
        const ldc_field_t *f = ldc_field_for_key(i);
        if (f == NULL)
            continue;
        CHECK(f->mask != 0);
        CHECK(((f->mask >> f->shift) << f->shift) == f->mask); // mask starts at shift
        for (size_t j = i + 1; j < CFG_COUNT; j++) {
            const ldc_field_t *g = ldc_field_for_key(j);
            if (g != NULL && g->reg == f->reg)
                CHECK((g->mask & f->mask) == 0); // no overlap
        }
    }
    CHECK_EQ(ldc_field_for_key(CFG_CH0_RCOUNT)->write, LDC_WRITE_ASLEEP);
    CHECK_EQ(ldc_field_for_key(CFG_ACTIVE_CHAN)->write, LDC_WRITE_PLAIN);
    CHECK_EQ(ldc_field_for_key(CFG_AUTOSCAN)->write, LDC_WRITE_CHECKED);
    CHECK_EQ(ldc_field_for_key(CFG_HIGH_CURRENT)->write, LDC_WRITE_CHECKED);
}

TEST(keys_without_a_field)
{
    CHECK(ldc_field_for_key(CFG_DEGLITCH) == NULL);
    for (size_t i = CFG_CH0_WINDOW; i <= CFG_CH1_THRESH_HIGH; i++)
        CHECK(ldc_field_for_key(i) == NULL);
    CHECK(ldc_field_for_key(CFG_COUNT) == NULL);
    CHECK(ldc_field_for_key(9999) == NULL);
}

TEST(held_key_mapping)
{
    ldc_held_t w;
    int ch;
    CHECK(ldc_held_key(CFG_CH0_WINDOW, &w, &ch) && w == LDC_HELD_WINDOW && ch == 0);
    CHECK(ldc_held_key(CFG_CH1_WINDOW, &w, &ch) && w == LDC_HELD_WINDOW && ch == 1);
    CHECK(ldc_held_key(CFG_CH0_THRESH_LOW, &w, &ch) && w == LDC_HELD_THRESH_LOW && ch == 0);
    CHECK(ldc_held_key(CFG_CH1_THRESH_HIGH, &w, &ch) && w == LDC_HELD_THRESH_HIGH && ch == 1);
    CHECK(!ldc_held_key(CFG_CH1_IDRIVE, &w, &ch));
    CHECK(!ldc_held_key(CFG_ACTIVE_CHAN, &w, &ch));
    CHECK(!ldc_held_key(CFG_COUNT, &w, &ch));
}

TEST(every_key_is_handled_exactly_one_way)
{
    for (size_t i = 0; i < CFG_COUNT; i++) {
        ldc_held_t w;
        int ch;
        int ways = (ldc_field_for_key(i) != NULL) + ldc_held_key(i, &w, &ch) + (i == CFG_DEGLITCH);
        CHECK_EQ(ways, 1);
    }
}

TEST(key_names_are_unique)
{
    for (int i = 0; i < CFG_COUNT; i++) {
        for (int j = i + 1; j < CFG_COUNT; j++) {
            CHECK(strcmp(ldc_cfgkeys[i].name, ldc_cfgkeys[j].name) != 0);
        }
    }
}

TEST(defaults_match_the_seeed_reference_config)
{
    // docs/09-sensor-configuration.md: RCOUNT 0x0546, SETTLECOUNT 30,
    // DRIVE_CURRENT 0xA000 (IDRIVE 20).
    CHECK_EQ(ldc_cfgkeys[CFG_CH0_RCOUNT].default_value, 0x0546);
    CHECK_EQ(ldc_cfgkeys[CFG_CH1_RCOUNT].default_value, 0x0546);
    CHECK_EQ(ldc_cfgkeys[CFG_CH0_SETTLECOUNT].default_value, 30);
    CHECK_EQ(ldc_field_pack(ldc_cfgkeys[CFG_CH0_IDRIVE].default_value, DRIVE_CURRENT_IDRIVE_MASK,
                            DRIVE_CURRENT_IDRIVE_SHIFT),
             0xA000);
}

int main(void)
{
    RUN_TEST(window_modes_and_boundaries);
    RUN_TEST(window_off_and_invalid_are_false);
    RUN_TEST(window_with_low_equal_high_and_inverted);
    RUN_TEST(thresholds_ok);
    RUN_TEST(present_is_never_stale);
    RUN_TEST(any_present_combines_channels);
    RUN_TEST(unpack_data);
    RUN_TEST(freq_math);
    RUN_TEST(clock_decode);
    RUN_TEST(active_channels);
    RUN_TEST(conversion_time);
    RUN_TEST(conversion_time_guards_a_zero_reference_clock);
    RUN_TEST(interval_is_clamped);
    RUN_TEST(field_pack_unpack_merge);
    RUN_TEST(field_round_trip_for_every_idrive);
    RUN_TEST(deglitch_codes_round_trip);
    RUN_TEST(mutual_exclusion_rules);
    RUN_TEST(poll_failure_counter);
    RUN_TEST(key_table_is_consistent);
    RUN_TEST(field_table_matches_the_register_layout);
    RUN_TEST(keys_without_a_field);
    RUN_TEST(held_key_mapping);
    RUN_TEST(every_key_is_handled_exactly_one_way);
    RUN_TEST(key_names_are_unique);
    RUN_TEST(defaults_match_the_seeed_reference_config);
    return tinytest_summary();
}
