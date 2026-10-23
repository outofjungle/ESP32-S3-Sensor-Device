#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "status_led_state.h"

// Drives the board's onboard RGB LED as a status indicator. See
// docs/10-led-status.md.
//
// Two modes: STATUS (the default -- the LED shows device state from the
// table below) and MANUAL (a host has taken direct control with SET led0).
// CFG LED_MODE switches between them explicitly; a SET led0 call also
// switches to MANUAL, since writing a color only makes sense if nothing
// else is about to overwrite it.
//
// | State            | Appearance                              |
// |------------------|------------------------------------------|
// | INIT             | Off                                       |
// | READY            | Green, 10% brightness, steady             |
// | STREAM_IDLE      | Blue, blink at 1 Hz                       |
// | STREAM_PRESENT   | Amber, blink at 1 Hz                      |
// | FAULT            | Red, steady                               |
//
// A one-shot white flash (status_led_flash_oneshot()) overlays whichever of
// these is current, for 60 ms, then reverts -- used for a one-shot GET of a
// sensor channel. On a board with no LED (board_get_profile()->led_gpio ==
// GPIO_NUM_NC), every function below is a no-op that returns ESP_OK.

esp_err_t status_led_init(void);

// Sets the status state shown while in STATUS mode. Has no visible effect
// while in MANUAL mode, but is remembered and resumes the instant MANUAL
// mode ends.
void status_led_set_state(status_led_state_t state);

// Overlays a 60 ms white flash on top of the current state or manual
// color, then reverts. Used for a one-shot GET of a sensor channel. See
// docs/10-led-status.md.
void status_led_flash_oneshot(void);

// Switches to MANUAL mode and shows this color. Used by the led0 channel's
// set() callback. Tells device_config about the mode switch so a later CFG
// LED_MODE read agrees.
esp_err_t status_led_set_manual_rgb(uint8_t r, uint8_t g, uint8_t b);

// Returns the color currently driving the LED: the last manual color in
// MANUAL mode, or the current status/flash color in STATUS mode. Used by
// the led0 channel's get() callback.
void status_led_get_rgb(uint8_t out[3]);

// Switches to STATUS mode, showing status_led_set_state()'s last value
// again. Called when CFG LED_MODE is set to "STATUS".
void status_led_use_status_mode(void);

// true if currently in MANUAL mode.
bool status_led_is_manual(void);
