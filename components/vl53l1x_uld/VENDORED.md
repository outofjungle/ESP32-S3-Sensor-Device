# Vendored library: VL53L1X Ultra Lite Driver (ULD)

This directory holds ST's VL53L1X Ultra Lite Driver (ULD) API, the reference software for the
VL53L1X time-of-flight sensor. See `docs/03-sensor-vl53l1x.md` and `docs/09-sensor-configuration.md`.

| Field | Value |
|---|---|
| Upstream project | `https://github.com/Saleca/vl53l1x_uld_esp_wrapper` (carries ST's `core/` unmodified) |
| Commit | `78bb3739b36d5402a4e958f9097212d2ce032ba6` |
| ULD version | 3.5.5, per `VL53L1X_IMPLEMENTATION_VER_*` in `include/VL53L1X_api.h` |
| License | BSD-3-Clause. See `LICENSE.txt` in this directory. |

Only ST's `core/` files are copied here: `VL53L1X_api.c/.h`, `VL53L1X_calibration.c/.h`,
`vl53l1_platform.h`, `vl53l1_types.h`. The upstream repository's `esp_wrapper/` directory,
which owns I2C transport and a different public API shape, is not copied. This project's own
I2C transport is `src/vl53l1x_platform_esp.c`, written against ESP-IDF's `driver/i2c_master.h`
and not part of this vendored drop.

## The one edit made to a vendored file

`include/vl53l1_platform.h` originally includes `"i2c_handler.h"`, a header private to the
upstream wrapper project. Nothing in that header is used by this file or by `VL53L1X_api.c`, so
the include is removed. No other line in any vendored file is changed.

## What this library does and does not cover

`VL53L1X_api.c` and `VL53L1X_calibration.c` are ST's sensor-level driver: ranging control,
result readout, distance mode, timing, region of interest, thresholds, and the two calibration
routines. It knows nothing about I2C, FreeRTOS, or ESP-IDF — it calls nine platform functions
declared in `vl53l1_platform.h` (`VL53L1_WrByte`, `WrWord`, `WrDWord`, `RdByte`, `RdWord`,
`RdDWord`, `WriteMulti`, `ReadMulti`, `WaitMs`), all nine of which this project implements in
`src/vl53l1x_platform_esp.c`.
