# Adding a Sensor

This document states the contract a new Grove sensor module implements, using `sensor_tof` and
`sensor_ldc` as worked examples. See `docs/00-overview.md` for term definitions, and
`docs/01-architecture.md` for where a sensor module sits in the dependency graph.

## What a sensor module must not do

A sensor module never includes `usb_cdc.h`, `proto_reply.h`, or `channel_registry.h` directly.
It talks only to `sensor_core.h`. This is what lets a sensor module be dropped in or left out
at build time with no change anywhere else.

## The four things a sensor module states

A sensor module is one `const sensor_driver_t` value (see `components/sensor_core/include/sensor_core.h`),
built from four parts:

1. **A channel list** -- a `sensor_channel_def_t` array. Each entry names one channel
   (unqualified, e.g. `"dist"` -- `sensor_core` joins it with the instance name), its declared
   type, its access mode, and a `get()` (and, for a writable channel, a `set()`) callback.
2. **A configuration key list** -- a `sensor_cfgkey_def_t` array. Each entry names one key,
   its type (`SENSOR_VAL_UINT`, `_INT`, `_BOOL`, or `_ENUM`), its valid range or enum values,
   and a default. `SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER` caps this list's length; raise it in
   `components/sensor_core/include/sensor_core.h` if a new sensor module needs more keys than
   it currently allows. `sensor_core`'s flash record for each instance sizes itself from the
   module's own `cfgkey_count`, so raising the cap does not disturb an instance that already
   has a saved configuration under the old, smaller cap.
3. **Lifecycle functions** -- `probe()`, `init()`, `start()`, `stop()`, and `poll()`.
4. **Configuration functions** -- `cfg_get()`, `cfg_set()`, and, if the sensor supports any
   action such as calibration, `action()`.

## Lifecycle, in the order `sensor_core_register()` calls it

| Step | Function | Called | Required to succeed? |
|---|---|---|---|
| 1 | `probe()` | Once, at registration | No -- a failure here still registers channels and configuration keys, in the `"fault"` state (see `SENS`, `docs/09-sensor-configuration.md`); it only skips the rest of this list. |
| 2 | `init()` | Once, right after a successful `probe()` | Yes, for the instance to reach the `"ok"` state. |
| 3 | `cfg_set()`, once per configuration key | Right after `init()` | Applies this instance's saved configuration, or each key's default if nothing was saved. |
| 4 | `start()` | Once, right after step 3 | Begins continuous sampling. |

After that, `sensor_core`'s one poll task calls `poll()` on its own schedule, at the interval
`poll_interval_ms()` reports -- which may itself read the chip, if the sensor's own cadence can
change at runtime.

## The one rule that matters most: `get()` must never block

A channel's `get()` callback runs on whichever task is asking -- the USB command task for a
one-shot `GET`, or the device-level sample timer while streaming. Neither of those may block
on a sensor's bus. `get()` reads a cached value `poll()` already wrote, protected by a
spinlock if more than one task can touch it. See `sensor_tof.c`'s `tof_ctx_t` and its
`ch_get_*()` functions for the pattern: every read is `portENTER_CRITICAL`, copy three or four
fields, `portEXIT_CRITICAL`, then encode. `poll()` is the only function allowed to do I2C.

## `cfg_get()` always reads the chip

Never a cached copy. This is what makes `SCFG`'s read-back verification real, not a claim. See
`docs/09-sensor-configuration.md`.

The one allowed exception: a key with no chip register behind it, because the sensor has no
hardware for what the key controls. `sensor_ldc`'s window and threshold keys are this case --
the LDC1612 has no window-detection hardware, so the firmware's own held value is the
authoritative value, and `cfg_get()` returns it directly. See "`cfg_get()` and keys with no
chip register" in `docs/09-sensor-configuration.md`. A key like this still goes through
`SCFG`'s normal validate-write-read-back-compare sequence; it only skips the chip.

## `cfg_set()` owns its own ordering rules

`sensor_core` calls `cfg_set()` once per key, in no particular order, and never assumes
anything about how a sensor's internal registers relate to each other. If two keys interact
(as `dist_mode` and `budget_ms` do for the VL53L1X), the sensor module's own `cfg_set()`
re-reads and re-asserts the related setting on every write, so the result is correct
regardless of call order. See `docs/09-sensor-configuration.md`, "Ordering rules".

## Using `sensor_core_get_user_ctx()` for per-instance state

A sensor module stores its own state (a cached sample, a mutex, anything else) behind
`sensor_core_set_user_ctx()`/`sensor_core_get_user_ctx()`, keyed to one `sensor_inst_t *`, not
in a bare file-scope global. `sensor_tof.c` uses a small fixed pool (`s_ctx_pool`,
`MAX_TOF_INSTANCES`) rather than allocating, since embedded firmware here avoids a heap
allocator for long-lived state. This is what lets more than one instance of the same sensor
module exist later, even though the AtomS3 Lite's one Grove port means only one is active
today.

## Registering the module

One function, exported from the module's own header, wraps `sensor_core_register()`:

```c
esp_err_t sensor_tof_register(const char *instance_name, const sensor_board_binding_t *binding)
{
    return sensor_core_register(&s_tof_driver, instance_name, binding, NULL);
}
```

A device assembly calls this from its `app_main.c`, inside an `#if defined(SENSOR_<NAME>)`
guard. See `docs/16-adding-a-device.md`.

## A new component, step by step

1. Create `components/sensor_<name>/`, with `include/sensor_<name>.h` and `src/sensor_<name>.c`.
2. Write the `CMakeLists.txt`: `REQUIRES sensor_core`, plus whatever the chip's own driver
   needs in `PRIV_REQUIRES`. A chip with a small register set, such as the LDC1612, does not
   need a separate vendored driver component -- `sensor_ldc.c` reaches the chip's registers
   directly, with `PRIV_REQUIRES esp_driver_i2c`.
3. Write the channel and configuration key arrays, the lifecycle functions, and the
   configuration functions, following the pattern above.
4. Write one public registration function.
5. Add the module to `main/devices/<name>/device.h` (an instance name and an I2C address) and
   `app_main.c` (an `#if defined(SENSOR_<NAME>)` registration block), following
   `docs/16-adding-a-device.md`.
6. Write the new document set: a `docs/NN-sensor-<name>.md` describing the chip, following
   `docs/03-sensor-vl53l1x.md`'s or `docs/19-sensor-ldc1612.md`'s shape; the module's channels,
   added to the table in `docs/08-channel-model-and-types.md`; the module's configuration
   keys, added as a new section in `docs/09-sensor-configuration.md`; and the module's own
   test steps, added as an appendix to `docs/18-test-plan.md`.
7. Add the module as a choice to `SENSOR` in `docs/14-build-and-flash.md`'s table, and build
   it with `make build SENSOR=<name>`.
8. Add a `web/<name>.html` control panel, and a link to it from the launcher,
   `web/index.html`. See `docs/17-web-control-panel.md`.

No change to `components/device_proto` is needed. `components/sensor_core` needs a change
only if the new module's configuration key count exceeds the current
`SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER` -- see "The four things a sensor module states", above.
