// Unit tests for components/device_proto/src/input_validate.c -- the
// range-check and string-termination helpers used to validate a value
// loaded from flash (NVS) or already decoded off the wire, before it is
// trusted. See AGENTS.md, "Firmware mistakes to avoid", and
// docs in this plan's review: Critical #3 (unvalidated NVS values).
//
// Build and run: see README.md (`make` in this directory).

#include "tinytest.h"
#include "input_validate.h"
#include <stdint.h>

/* ---------------------------------------------------------------------
 * iv_u32_in_range / iv_i32_in_range
 * ------------------------------------------------------------------- */

TEST(u32_in_range_boundaries)
{
    CHECK(iv_u32_in_range(0, 0, 10) == true);
    CHECK(iv_u32_in_range(10, 0, 10) == true); // inclusive upper bound
    CHECK(iv_u32_in_range(5, 0, 10) == true);
    CHECK(iv_u32_in_range(11, 0, 10) == false); // one past the upper bound
    CHECK(iv_u32_in_range(0, 1, 10) == false);  // below the lower bound
    CHECK(iv_u32_in_range(1, 1, 10) == true);   // inclusive lower bound
}

TEST(u32_in_range_full_width)
{
    // device_config.c validates period_us this way, with max ==
    // UINT32_MAX -- the upper check must never spuriously reject the
    // largest representable value.
    CHECK(iv_u32_in_range(UINT32_MAX, 1000, UINT32_MAX) == true);
    CHECK(iv_u32_in_range(999, 1000, UINT32_MAX) == false);
}

TEST(i32_in_range_negative_bounds)
{
    // sensor_tof's tof0.offset_mm key has min=-1024, max=1023 -- signed
    // range checks must handle a negative lower bound correctly.
    CHECK(iv_i32_in_range(-1024, -1024, 1023) == true);
    CHECK(iv_i32_in_range(1023, -1024, 1023) == true);
    CHECK(iv_i32_in_range(0, -1024, 1023) == true);
    CHECK(iv_i32_in_range(-1025, -1024, 1023) == false);
    CHECK(iv_i32_in_range(1024, -1024, 1023) == false);
}

/* ---------------------------------------------------------------------
 * iv_bool_in_range
 * ------------------------------------------------------------------- */

TEST(bool_in_range)
{
    CHECK(iv_bool_in_range(0) == true);
    CHECK(iv_bool_in_range(1) == true);
    CHECK(iv_bool_in_range(-1) == false);
    CHECK(iv_bool_in_range(2) == false);
    CHECK(iv_bool_in_range(268435455) == false); // a plausible garbage NVS value
}

/* ---------------------------------------------------------------------
 * iv_enum_in_range -- this is the one directly behind Critical #3
 * (sensor_ldc.c's s_deglitch_codes[value] out-of-bounds read/panic).
 * ------------------------------------------------------------------- */

TEST(enum_in_range_boundaries)
{
    // sensor_ldc's deglitch key has 4 valid values (indices 0-3).
    CHECK(iv_enum_in_range(0, 4) == true);
    CHECK(iv_enum_in_range(3, 4) == true);  // last valid index
    CHECK(iv_enum_in_range(4, 4) == false); // exactly one past the end
    CHECK(iv_enum_in_range(-1, 4) == false);
}

TEST(enum_in_range_the_exact_bug_this_fixes)
{
    // The concrete scenario from the review: a saved cfgkey blob maps
    // onto the wrong key after a future enum reorder, landing a
    // large value (here, a plausible stand-in for a stray
    // ch0_thresh_high value, 268435455) on a 4-entry enum. This must be
    // rejected, not used as an array index.
    CHECK(iv_enum_in_range(268435455, 4) == false);
}

TEST(enum_in_range_zero_count)
{
    // A driver bug (an ENUM key declared with enum_count 0) must not
    // make every value look valid; nothing is a valid index into an
    // empty table.
    CHECK(iv_enum_in_range(0, 0) == false);
}

/* ---------------------------------------------------------------------
 * iv_force_terminate -- this is the one directly behind the
 * device_config.c half of Critical #3 (an unterminated mode/led_mode/
 * subs[] string from a corrupted NVS blob reaching strcmp/strlen).
 * ------------------------------------------------------------------- */

TEST(force_terminate_untouched_when_already_short)
{
    char buf[8] = "hi";
    iv_force_terminate(buf, sizeof(buf));
    CHECK_STREQ(buf, "hi");
}

TEST(force_terminate_un_terminated_buffer)
{
    // Simulates exactly what a corrupted/short NVS read can leave: a
    // fixed buffer with no NUL anywhere in it.
    char buf[8];
    memset(buf, 'A', sizeof(buf));
    iv_force_terminate(buf, sizeof(buf));
    CHECK_EQ(buf[sizeof(buf) - 1], '\0');
    CHECK_EQ(strlen(buf), (size_t)7); // the first 7 bytes are still 'A', now terminated
}

TEST(force_terminate_exact_fit_string)
{
    // A string that fills every byte but the last is left alone;
    // one that fills all bufsz bytes gets its last byte overwritten.
    char buf[4] = { 'A', 'B', 'C', 'X' }; // no room for a NUL at all
    iv_force_terminate(buf, sizeof(buf));
    CHECK_EQ(buf[3], '\0');
    CHECK_STREQ(buf, "ABC");
}

TEST(force_terminate_zero_size_is_a_no_op)
{
    // Must not write buf[-1] or otherwise go out of bounds.
    char buf[1] = { 'Z' };
    iv_force_terminate(buf, 0);
    CHECK_EQ(buf[0], 'Z'); // untouched
}

TEST(force_terminate_null_buffer_does_not_crash)
{
    // Defensive: a caller passing NULL (should not happen, but this is
    // exactly the class of assumption that caused the original bugs)
    // must not crash the test process.
    iv_force_terminate(NULL, 8);
    CHECK(true); // reaching this line at all is the assertion
}

TEST(force_terminate_single_byte_buffer)
{
    char buf[1] = { 'Z' };
    iv_force_terminate(buf, sizeof(buf));
    CHECK_EQ(buf[0], '\0');
}

int main(void)
{
    RUN_TEST(u32_in_range_boundaries);
    RUN_TEST(u32_in_range_full_width);
    RUN_TEST(i32_in_range_negative_bounds);
    RUN_TEST(bool_in_range);
    RUN_TEST(enum_in_range_boundaries);
    RUN_TEST(enum_in_range_the_exact_bug_this_fixes);
    RUN_TEST(enum_in_range_zero_count);
    RUN_TEST(force_terminate_untouched_when_already_short);
    RUN_TEST(force_terminate_un_terminated_buffer);
    RUN_TEST(force_terminate_exact_fit_string);
    RUN_TEST(force_terminate_zero_size_is_a_no_op);
    RUN_TEST(force_terminate_null_buffer_does_not_crash);
    RUN_TEST(force_terminate_single_byte_buffer);
    return tinytest_summary();
}
