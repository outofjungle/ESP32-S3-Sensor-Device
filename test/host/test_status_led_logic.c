// Unit tests for components/status_led/src/status_led_logic.c.
#include "tinytest.h"
#include "status_led_logic.h"

static bool rgb_is(const uint8_t c[3], uint8_t r, uint8_t g, uint8_t b)
{
    return c[0] == r && c[1] == g && c[2] == b;
}

TEST(state_colors_in_both_phases)
{
    uint8_t c[3];
    led_state_color(LED_STATE_INIT, true, c);
    CHECK(rgb_is(c, 0, 0, 0));
    led_state_color(LED_STATE_INIT, false, c);
    CHECK(rgb_is(c, 0, 0, 0));
    led_state_color(LED_STATE_READY, true, c);
    CHECK(rgb_is(c, 0, 26, 0));
    led_state_color(LED_STATE_READY, false, c);
    CHECK(rgb_is(c, 0, 26, 0)); // steady
    led_state_color(LED_STATE_STREAM_IDLE, true, c);
    CHECK(rgb_is(c, 0, 0, 255));
    led_state_color(LED_STATE_STREAM_IDLE, false, c);
    CHECK(rgb_is(c, 0, 0, 0));
    led_state_color(LED_STATE_STREAM_PRESENT, true, c);
    CHECK(rgb_is(c, 255, 140, 0));
    led_state_color(LED_STATE_STREAM_PRESENT, false, c);
    CHECK(rgb_is(c, 0, 0, 0));
    led_state_color(LED_STATE_FAULT, true, c);
    CHECK(rgb_is(c, 255, 0, 0));
    led_state_color(LED_STATE_FAULT, false, c);
    CHECK(rgb_is(c, 255, 0, 0)); // steady
}

TEST(unknown_state_is_the_fault_colour)
{
    uint8_t c[3];
    led_state_color((status_led_state_t)42, true, c);
    CHECK(rgb_is(c, 255, 0, 0));
}

TEST(pick_priority_flash_then_manual_then_state)
{
    const uint8_t manual[3] = { 1, 2, 3 };
    uint8_t c[3];
    led_pick_rgb(true, true, manual, LED_STATE_FAULT, true, c);
    CHECK(rgb_is(c, 255, 255, 255));
    led_pick_rgb(false, true, manual, LED_STATE_FAULT, true, c);
    CHECK(rgb_is(c, 1, 2, 3));
    led_pick_rgb(false, false, manual, LED_STATE_FAULT, true, c);
    CHECK(rgb_is(c, 255, 0, 0));
    led_pick_rgb(true, false, manual, LED_STATE_READY, true, c);
    CHECK(rgb_is(c, 255, 255, 255));
}

TEST(blink_toggles_every_ten_ticks)
{
    led_tick_t t;
    led_tick_init(&t);
    led_tick_set_state(&t, LED_STATE_STREAM_IDLE);
    for (int i = 1; i < LED_BLINK_TOGGLE_TICKS; i++) {
        CHECK(!led_tick_step(&t)); // ticks 1..9: no redraw
        CHECK(t.blink_phase_on);
    }
    CHECK(led_tick_step(&t)); // tick 10: toggled, redraw
    CHECK(!t.blink_phase_on);
    for (int i = 1; i < LED_BLINK_TOGGLE_TICKS; i++)
        led_tick_step(&t);
    CHECK(led_tick_step(&t));
    CHECK(t.blink_phase_on);
}

TEST(no_redraw_on_toggle_for_steady_states)
{
    status_led_state_t steady[] = { LED_STATE_INIT, LED_STATE_READY, LED_STATE_FAULT };
    for (size_t k = 0; k < 3; k++) {
        led_tick_t t;
        led_tick_init(&t);
        led_tick_set_state(&t, steady[k]);
        for (int i = 0; i < 4 * LED_BLINK_TOGGLE_TICKS; i++)
            CHECK(!led_tick_step(&t));
    }
}

TEST(present_state_also_blinks)
{
    led_tick_t t;
    led_tick_init(&t);
    led_tick_set_state(&t, LED_STATE_STREAM_PRESENT);
    for (int i = 1; i < LED_BLINK_TOGGLE_TICKS; i++)
        led_tick_step(&t);
    CHECK(led_tick_step(&t));
}

TEST(set_state_restarts_the_blink_in_the_on_phase)
{
    led_tick_t t;
    led_tick_init(&t);
    led_tick_set_state(&t, LED_STATE_STREAM_IDLE);
    for (int i = 0; i < LED_BLINK_TOGGLE_TICKS; i++)
        led_tick_step(&t); // now off
    CHECK(!t.blink_phase_on);
    led_tick_set_state(&t, LED_STATE_STREAM_PRESENT);
    CHECK(t.blink_phase_on);
    CHECK_EQ(t.blink_counter, 0u);
}

TEST(flash_lasts_two_steps_then_ends)
{
    led_tick_t t;
    led_tick_init(&t);
    led_tick_set_state(&t, LED_STATE_READY);
    CHECK(!led_tick_flashing(&t));
    led_tick_start_flash(&t);
    CHECK(led_tick_flashing(&t));
    CHECK(led_tick_step(&t)); // step 1: still flashing, redraw
    CHECK(led_tick_flashing(&t));
    CHECK(led_tick_step(&t)); // step 2: flash ends, redraw to restore colour
    CHECK(!led_tick_flashing(&t));
    CHECK(!led_tick_step(&t)); // nothing left to draw
}

TEST(flash_restart_extends_it)
{
    led_tick_t t;
    led_tick_init(&t);
    led_tick_start_flash(&t);
    led_tick_step(&t);
    led_tick_start_flash(&t);
    CHECK_EQ(t.flash_ticks_remaining, LED_FLASH_TICKS + 1);
}

TEST(timing_constants)
{
    CHECK_EQ(LED_TICK_MS * LED_BLINK_TOGGLE_TICKS, 500); // 1 Hz blink: toggles every 500 ms
}

int main(void)
{
    RUN_TEST(state_colors_in_both_phases);
    RUN_TEST(unknown_state_is_the_fault_colour);
    RUN_TEST(pick_priority_flash_then_manual_then_state);
    RUN_TEST(blink_toggles_every_ten_ticks);
    RUN_TEST(no_redraw_on_toggle_for_steady_states);
    RUN_TEST(present_state_also_blinks);
    RUN_TEST(set_state_restarts_the_blink_in_the_on_phase);
    RUN_TEST(flash_lasts_two_steps_then_ends);
    RUN_TEST(flash_restart_extends_it);
    RUN_TEST(timing_constants);
    return tinytest_summary();
}
