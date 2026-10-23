# Test Plan

This document lists the acceptance tests for this version. See `docs/00-overview.md` for term
definitions.

The core suite below applies to both sensor modules. Run it against each build
(`SENSOR=tof` and `SENSOR=ldc`). Then run the one appendix matching whichever sensor module
that build has, in addition: Appendix A for `SENSOR=tof`, Appendix B for `SENSOR=ldc`.

## Hardware safety check

Do this test before any other test in this document.

| Step | Action | Expected result |
|---|---|---|
| 1 | Check the Grove cable's wiring against the sensor module's own document (`docs/03-sensor-vl53l1x.md` or `docs/19-sensor-ldc1612.md`) before connecting it. | Black to GND, red to 5V, yellow to SDA, white to SCL. |

## Core suite: host unit tests, with no hardware

| Step | Action | Expected result |
|---|---|---|
| 1a | `make host-test` | Every test binary reports all tests passed. The command exits with status 0. |
| 1b | `make host-coverage` | The report shows 95% or more of the lines covered in every file listed. |

## Core suite: build tests, with no hardware

| Step | Action | Expected result |
|---|---|---|
| 2 | `make image-build`, then `make build`. | Builds with no error. The image is `espressif/idf:v6.1`. |
| 3 | Check `dependencies.lock`. | `esp_tinyusb` resolves to a 2.4.x or later version; `led_strip` resolves to 3.1.x or later. |
| 4 | `make build SENSOR=none`. | Builds and links. `build/project_description.json` lists no `sensor_tof` or `sensor_ldc` component. |
| 5 | Compare the firmware binary size between steps 2 and 4. | The `SENSOR=none` build is smaller. |
| 6 | `make build DEVICE=nosuchdevice`. | Fails with a readable message naming the missing directory, not a cryptic CMake error. |
| 7 | `make build SENSOR=ldc`. | Builds with no error. |
| 8 | Check `build/project_description.json` for the `SENSOR=ldc` build. | Lists `sensor_ldc`. Does not list `sensor_tof` or `vl53l1x_uld`. |

## Core suite: web panel tests, with no hardware

| Step | Action | Expected result |
|---|---|---|
| 9 | Open `web/index.html` directly. | The launcher lists both sensor panels. |
| 10 | Open `web/vl53l1x.html` directly, then `web/ldc1612.html` directly. | On each: the startup self-test passes; Connect is enabled. |
| 11 | Open `web/vl53l1x.html?mock=1`, then `web/ldc1612.html?mock=1`. | On each: Connect succeeds against the mock device. |
| 12 | Open either page with `&fault=crc` added. | The CRC error counter (visible through `STAT`) increases; the page does not stop working. |
| 13 | Open either page with `&fault=drop` added. | A sample eventually carries the `DROP` flag; the page shows this. |

## Core suite: build and connection tests, on hardware

| Step | Action | Expected result |
|---|---|---|
| 14 | Enter download mode (`docs/02-board-atoms3-lite.md`), then `make flash`. | Flashing succeeds. |
| 15 | `make monitor`, holding the button through reset. | Log output appears on CDC1. |
| 16 | Open the web panel matching the flashed `SENSOR`, click Connect, choose the device. | Connects; `ID` shows a name, firmware version, and serial number. |
| 17 | Reload the page and click Connect again, with no manual port choice. | Reconnects automatically (via `probeGrantedPorts()`), since the port was already granted. |

## Core suite: command tests

| Step | Action | Expected result |
|---|---|---|
| 18 | `PING` | `[1, seq, "PING"]` |
| 19 | `CAPS` | Lists `led0`, `uptime_us`, and every channel the built sensor module defines (`docs/08-channel-model-and-types.md`). |
| 20 | `SET` on a read-only sensor channel, e.g. `SET tof0.dist 100` or `SET ldc0.ch0_raw 100`. | `EACCESS` |
| 21 | An unknown verb, e.g. `FOO`. | `ECMD` |

## Core suite: LED tests

| Step | Action | Expected result |
|---|---|---|
| 22 | At boot, before any command. | LED is off. |
| 23 | After boot, not streaming. | LED is green, steady, 10% brightness. |
| 24 | `GET` on any sensor channel. | LED flashes white briefly, then returns to its previous state. |
| 25 | `SET led0` with a color. | LED shows that color, steady, and `CFG LED_MODE` now reads `MANUAL`. |
| 26 | `CFG LED_MODE STATUS` | LED returns to showing device state. |

## Core suite: failure and recovery tests

| Step | Action | Expected result |
|---|---|---|
| 27 | Unplug the Grove cable while streaming. | LED turns red (fault). Every channel the built sensor module defines reports `null`. |
| 28 | Reconnect the Grove cable. | The device recovers without a restart, once the next probe succeeds -- note: in this version, `probe()` runs once at boot, so recovery needs `RST`, not a live retry. |
| 29 | Send a frame with a deliberately wrong CRC byte. | No `ERR` response; the `crc_errors` counter in `STAT` increases by one. |

## Core suite: debug port tests

| Step | Action | Expected result |
|---|---|---|
| 30 | Hold the button through boot, then check the host's port list. | A second serial port (CDC1) appears. |
| 31 | `make monitor` on that port. | Shows this firmware's `ESP_LOGx` output. |

## Appendix A: VL53L1X (`SENSOR=tof`)

Run these against a `SENSOR=tof` build, in addition to the core suite above.

| Step | Action | Expected result |
|---|---|---|
| A1 | After connecting (mock) to `web/vl53l1x.html`, check the configuration card. | Every `tof0.*` key from `docs/09-sensor-configuration.md` appears, with the right type of control (a dropdown for an enum, a number field otherwise). |
| A2 | Change `tof0.thresh_low_mm` or `tof0.thresh_high_mm` in the mock. | The shaded band on the distance chart moves to match. |
| A3 | `GET tof0.dist` while a target is in range. | A positive integer, in millimeters. |
| A4 | `SENS` | One row: `tof0`, `vl53l1x`, `i2c`, `0x29` (41 decimal), `ok`. |
| A5 | `SCAPS` | All 14 `tof0.*` configuration keys, each matching `docs/09-sensor-configuration.md`. |
| A6 | `SCFG tof0.dist_mode SHORT`, then `SCFG tof0.budget_ms 15`. | Both succeed. |
| A7 | `SCFG tof0.dist_mode LONG`, then `SCFG tof0.budget_ms 15`. | `ERANGE` -- 15 ms needs `SHORT` mode. |
| A8 | `SCFG tof0.roi_x 8`, then `SCFG tof0.roi_x`. | The read-back matches 8. |
| A9 | Measure a known distance with `roi_x`/`roi_y` at 16, then at 4. | The two readings can differ, since a narrower region of interest can see a different part of the scene. |
| A10 | `SCFG tof0.window OFF` | Succeeds. |
| A11 | `GET tof0.present` | `null` |
| A12 | `GET tof0.dist` a few times, a few seconds apart, with `window` still `OFF`. | Keeps updating. |
| A13 | `SCFG tof0.window IN`, `SCFG tof0.thresh_low_mm 50`, `SCFG tof0.thresh_high_mm 1500`. | All succeed. |
| A14 | Move a hand into, then out of, 50-1500 mm from the sensor. | `tof0.present` tracks it, `true` then `false`. |
| A15 | While a hand stays still in range, check consecutive `DATA` samples for `tof0.dist`. | Some samples carry the `STALE` flag, once the distance stops changing between polls. |
| A16 | `SUB tof0.present`, `START`, with nothing in range. | LED blinks blue at 1 Hz. |
| A17 | Move a hand into range. | LED switches to blinking amber at 1 Hz. |
| A18 | `STOP` (if streaming), then `CAL tof0.offset 100` against a flat target at 100 mm. | Takes a few seconds; returns an offset value. |
| A19 | `SAVE`, then power-cycle the device, then `SCFG tof0.offset_mm`. | The offset from step A18 persisted. |
| A20 | `START`, then `CAL tof0.offset 100`. | `ESTATE` -- calibration is not allowed while streaming. |
| A21 | `SCFG tof0.offset_mm -1023`, then `SCFG tof0.offset_mm`. | The read-back matches -1023, not `EVERIFY`. |
| A22 | `SCFG tof0.signal_kcps 1025`, then `SCFG tof0.signal_kcps`. | The read-back matches 1025, not `EVERIFY`, although the chip stores 1024. |
| A23 | `SCFG tof0.sigma_mm 16384` | `ERANGE`. |

## Appendix B: LDC1612 (`SENSOR=ldc`)

Run these against a `SENSOR=ldc` build, in addition to the core suite above.

| Step | Action | Expected result |
|---|---|---|
| B1 | After connecting (mock) to `web/ldc1612.html`, check the configuration card. | All 22 `ldc0.*` keys from `docs/09-sensor-configuration.md` appear, with the right type of control. |
| B2 | In the mock, set `ldc0.ch0_window` to `IN` with thresholds around the simulated baseline. | The channel 0 presence badge changes to match. |
| B3 | `SENS` | One row: `ldc0`, `ldc1612`, `i2c`, `0x2B` (43 decimal), `ok`. A `fault` state means `probe()` did not read manufacturer ID `0x5449` and device ID `0x3055` -- check the `ADDR` pin first. |
| B4 | `SCAPS` | All 22 `ldc0.*` configuration keys, each matching `docs/09-sensor-configuration.md`. |
| B5 | `SCFG ldc0.ch0_rcount 2000`, then `SCFG ldc0.ch0_rcount`. | The read-back matches 2000, not `EVERIFY`. |
| B6 | Repeat step B5 for every key in `docs/09-sensor-configuration.md`'s LDC1612 table, with a value inside each key's range. | Every read-back matches what was written. An `EVERIFY` on any key means its `cfg_get()` does not invert its `cfg_set()`. |
| B7 | `SCFG ldc0.high_current true`, while `ldc0.autoscan` is `true`. | `ERANGE`. |
| B8 | `SCFG ldc0.autoscan false`, `SCFG ldc0.high_current true`, then `SCFG ldc0.autoscan true`. | The third command fails `ERANGE` -- the reverse of step B7. |
| B9 | `SCFG ldc0.high_current false`, then `SCFG ldc0.autoscan true`. | Succeeds, restoring the default configuration. |
| B10 | Move a coin over coil 0, then away. | `ldc0.ch0_raw` changes. `ldc0.ch1_raw` does not. |
| B11 | Move a coin over coil 1, then away. | `ldc0.ch1_raw` changes. `ldc0.ch0_raw` does not. Together with step B10, this confirms the two channels are not crossed. |
| B12 | `SCFG ldc0.autoscan false`, then `SCFG ldc0.active_chan CH0`. | Both succeed. |
| B13 | `GET ldc0.ch1_raw`, with the configuration from step B12 still set. | `null` -- channel 1 is not the active channel. |
| B14 | `SCFG ldc0.ch0_window IN`, then a `thresh_low` below and a `thresh_high` above the no-target raw count for channel 0. | All succeed. |
| B15 | Move a coin into, then out of, the band configured in step B14, over coil 0. | `ldc0.ch0_present` tracks it, `true` then `false`. |
| B16 | `SAVE`, then power-cycle the device, then `SCFG ldc0.ch0_window`. | The value from step B14 persisted, including this firmware-held key. |
| B17 | `CAL ldc0.ch0 100` (or any `CAL` naming an `ldc0` key). | `ENOCH` -- `sensor_ldc` defines no action. |
| B18 | `SCFG ldc0.ch0_thresh_low 5000`, then `SCFG ldc0.ch0_thresh_high 4000`. | The second command fails `ERANGE`. |
| B19 | With `ldc0.ch0_window` `IN` and a coin over coil 0, unplug the Grove cable. | Within 5 poll cycles `SENS` reports `fault`. `GET ldc0.ch0_present` does not stay `true`. |
