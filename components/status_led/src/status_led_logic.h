#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "status_led_state.h"

// Pure colour and blink logic split out of status_led.c so it runs in the
// host-side unit tests (test/host/test_status_led_logic.c).

#define LED_TICK_MS            50
#define LED_BLINK_TOGGLE_TICKS 10 // 10 * 50 ms = 500 ms -> 1 Hz blink
#define LED_FLASH_TICKS        1  // 60 ms rounds up to one 50 ms tick, plus the tick in flight

// The colour for a status state. phase_on only matters for the blinking
// states. An unknown state gives red, the fault colour.
void led_state_color(status_led_state_t state, bool phase_on, uint8_t out[3]);

// What to show: a one-shot flash overrides a manual colour, which overrides
// the status colour.
void led_pick_rgb(bool flashing, bool manual, const uint8_t manual_rgb[3], status_led_state_t state, bool phase_on,
                  uint8_t out[3]);

typedef struct {
    status_led_state_t state;
    uint32_t blink_counter;
    bool blink_phase_on;
    int flash_ticks_remaining;
} led_tick_t;

void led_tick_init(led_tick_t *t);

// A new state restarts the blink in its "on" phase.
void led_tick_set_state(led_tick_t *t, status_led_state_t state);

// Starts the white flash. One extra tick covers the tick already in flight.
void led_tick_start_flash(led_tick_t *t);

bool led_tick_flashing(const led_tick_t *t);

// Advances one 50 ms tick. Returns true if the LED needs redrawing: a flash
// step happened, or the blink toggled while a blinking state is showing.
bool led_tick_step(led_tick_t *t);
