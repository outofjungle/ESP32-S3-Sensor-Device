# LED Status

This document describes the onboard RGB LED's states, and the manual-override rule. See
`docs/00-overview.md` for term definitions.

## The two modes

The LED has two modes, held in the device-level `LED_MODE` configuration key (see
`docs/07-command-reference.md`).

| Mode | Meaning |
|---|---|
| `STATUS` (default) | The LED shows device state, from the table below. |
| `MANUAL` | A host has taken direct control, with `SET led0`. |

Writing `led0` switches to `MANUAL` automatically -- a write only makes sense if nothing else
is about to overwrite it. Setting `LED_MODE` back to `STATUS` returns to showing device state,
and resumes exactly where the state machine left off.

## States, in `STATUS` mode

| State | Appearance | When |
|---|---|---|
| Off | No light | While the sensor is still initializing, at boot. |
| Green, 10% brightness, steady | Ready | Not streaming, and every sensor instance probed successfully. |
| Blue, blinking at 1 Hz | Streaming, nothing present | Streaming is on, and the `tof0.present` channel (or the equivalent for whatever sensor module is built in) currently reads `false` or `null`. |
| Amber, blinking at 1 Hz | Streaming, something present | Streaming is on, and that same presence channel currently reads `true`. For `SENSOR=ldc`, this is `true` when either `ldc0.ch0_present` or `ldc0.ch1_present` reads `true`. |
| Red, steady | Fault | Any registered sensor instance's probe step failed. See `SENS` in `docs/09-sensor-configuration.md`. |

Fault takes priority over every other state: a device with a failed sensor shows red
regardless of whether it is streaming. See `docs/20-state-machines.md` for this state
machine, as a diagram.

## One-shot flash

Any `GET` command -- of any channel, single or `"*"` -- flashes the LED white for 60
milliseconds, then returns to whatever it was showing before. This is a visible confirmation
that a one-shot read reached the device, separate from the continuous blink a streaming
session shows.

## Which code owns this

`components/status_led` owns the state machine and the flash. It knows nothing about any
specific sensor. The decision of which status state to show -- in particular, reading a
sensor-specific presence channel to choose between the two streaming states -- is made in
`main/devices/atoms3_lite/app_main.c`, the one file in this project that is allowed to know
about both the board and the sensor module built in. See `docs/01-architecture.md`.

## A board with no LED

`board_profile_t`'s `led_gpio` field is `GPIO_NUM_NC` for a board with no RGB LED.
`status_led_init()` detects this and every other function in `status_led.h` becomes a no-op
that still returns `ESP_OK`, so a sensor-only device with no LED builds and runs unchanged.
