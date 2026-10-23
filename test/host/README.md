# Host-compiled unit tests

These tests build and run on your own computer, with `cc`/`make` only --
no Docker, no ESP-IDF, no target hardware. They cover the pure-logic
pieces of the firmware: functions with no FreeRTOS or ESP-IDF
dependency, extracted so they can be checked this way. See
`components/device_proto/src/input_validate.c` and
`components/device_proto/src/proto_util.c`.

This is not a general-purpose test framework for the firmware. Most of
the firmware (I2C drivers, the poll task, the USB transport) needs
either the target chip or a much larger simulation effort; that is
still covered only by the manual procedure in `docs/18-test-plan.md`.
These tests cover the one class of bug that does not need hardware to
reproduce: wrong bounds-checking and string-termination logic.

## Run

From the repository root:

```sh
make host-test
```

Or, directly in this directory:

```sh
cd test/host
make test
```

Each binary prints one line per failing check and a pass/fail count at
the end, and the `test` target's exit code is non-zero if anything
failed -- safe to wire into CI later.

Run `make check` from the repository root to also check formatting and run `cppcheck`.

## Coverage in VS Code

1. Install gcovr: `brew install gcovr`.
2. Install the VS Code extension "Coverage Gutters" (`ryanluker.vscode-coverage-gutters`).
3. Run `make host-coverage` from the repository root. It writes `test/host/coverage.info`.
4. Open `components/device_proto/src/proto_util.c`, then run "Coverage Gutters: Display Coverage"
   from the command palette. Covered lines show green, uncovered lines show red.

The report covers the files listed under "What's covered".

## What's covered

Each row is one test binary. `make host-coverage` reports 99% of the lines in these files.

| Test binary | Code under test | What it checks |
|---|---|---|
| `test_input_validate` | `input_validate.c` | Range, enum, and string-termination checks for values loaded from flash. |
| `test_proto_util` | `proto_util.c` | The CBOR text copy that always leaves a terminated string. |
| `test_cobs` | `cobs.c` | Encode and decode vectors, buffer limits, malformed input, round trip for 0 to 600 bytes. |
| `test_crc16` | `crc16.c` | The standard check value and the two worked examples in `docs/06-cbor-encoding.md`. |
| `test_proto_frame` | `proto_frame.c` | Frame encode and decode, bit flips, size limits, the 256-byte wire limit. |
| `test_channel_registry` | `channel_registry.c` | Access strings, limits, name length, duplicates, NULL arguments. |
| `test_sensor_cfg` | `sensor_cfg_logic.c` | SCFG value parsing, saved-record pack and unpack, instance state rules. |
| `test_ldc_logic` | `ldc_logic.c` | LDC1612 window, frequency, clock, interval, and register-field math; the key table. |
| `test_tof_logic` | `tof_logic.c` | VL53L1X timing rules, window decode, calibration bounds, and ST register arithmetic. |
| `test_status_led_logic` | `status_led_logic.c` | LED colors, blink timing, and the flash overlay. |
| `test_device_config` | `device_config.c`, `device_config_logic.c` | Subscriptions, setters, save and load against a fake flash, repair of a corrupt record. |
| `test_proto_cmd` | `proto_cmd.c`, `proto_reply.c` | Every verb through the dispatcher, with a fake transmit function that captures replies. |
| `test_rx_accum` | `rx_accum.c` | Splitting host bytes into frames, including oversize frames and recovery. |
| `test_stream_logic` | `stream_logic.c` | Sample sequence wrap and sample frame building. |

## Stub headers

`stubs/` holds small stand-ins for ESP-IDF and FreeRTOS headers (`esp_err.h`, `nvs.h`,
`freertos/FreeRTOS.h`, and others). A test that needs one of the firmware's `.c` files that uses
those headers includes the `.c` file directly. That also lets the test reset the file's static state
between cases.

Set `TINYTEST_VERBOSE=1` to print each test name before it runs. This shows which test crashed.

## Adding a test for a new pure-logic function

If a function you are adding has no FreeRTOS/ESP-IDF dependency (no
`i2c_master_*`, no `xSemaphore*`, no `esp_log`, no `nvs_*`), consider
writing it this way from the start, with its own `test_<name>.c` here,
rather than only compile-testing it inside the full firmware. A
function that only compiles, versus a function whose boundary cases are
checked, are not the same claim -- see `AGENTS.md`, "Firmware mistakes
to avoid".
