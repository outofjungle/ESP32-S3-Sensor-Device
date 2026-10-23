#pragma once

// The status states the LED can show. Free of ESP-IDF headers so the colour
// and blink logic in status_led_logic.c runs in the host-side unit tests.
// See docs/10-led-status.md and docs/20-state-machines.md.
typedef enum {
    LED_STATE_INIT,
    LED_STATE_READY,
    LED_STATE_STREAM_IDLE,
    LED_STATE_STREAM_PRESENT,
    LED_STATE_FAULT,
} status_led_state_t;
