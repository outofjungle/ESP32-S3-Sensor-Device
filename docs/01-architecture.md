# Architecture

This document describes the components and how they depend on each other. See
`docs/00-overview.md` for term definitions.

## The components

| Component | Owns | Knows about |
|---|---|---|
| `device_proto` | COBS, the CRC, CBOR framing, USB CDC transport, the channel registry, device-level stream configuration (`CFG`), and the command dispatcher. | Nothing about sensors. |
| `sensor_core` | The `sensor_driver_t` interface, the sensor instance registry, the sensor configuration-key registry, the poll task, and the `SENS`/`SCAPS`/`SCFG`/`CAL` commands. | Nothing about any specific chip. It calls into `device_proto` to register channels and the commands above, and to join `SAVE`/`LOAD`. |
| `sensor_tof` | The VL53L1X sensor module: a `sensor_driver_t` implementation. | The vendored ULD driver and `sensor_core`. Nothing about the wire protocol. |
| `vl53l1x_uld` | ST's VL53L1X Ultra Lite Driver (ULD), vendored, plus this project's own ESP-IDF I2C transport for it. | The chip's register map. Nothing about sensor_core or the wire protocol. |
| `sensor_ldc` | The LDC1612 sensor module: a `sensor_driver_t` implementation. | `sensor_core`, and the chip's own register map directly -- no vendored driver component, since the LDC1612's register set is small enough to reach over I2C from within this component itself. Nothing about the wire protocol. |
| `board` | Pin and bus assignments, one profile header per board. | Nothing above it. |
| `status_led` | The onboard RGB LED's status state machine. | `device_proto` (for streaming state) and `board` (for the LED pin). |
| `main/devices/<name>` | Which board, which sensor instance names, init order, the device's own channels (`led0`, `uptime_us`). | Everything. This is the one place allowed to be aware of both the board and the sensor module. |

## Dependency direction

```
main/devices/<name>
    |
    +--> sensor_tof --> sensor_core --> device_proto
    |         \
    |          \--> vl53l1x_uld
    |
    +--> sensor_ldc --> sensor_core --> device_proto
    |
    +--> status_led --> device_proto
    |         \--------> board
    |
    +--> board
    +--> device_proto
```

Only one of `sensor_tof` and `sensor_ldc` is built into a given firmware image; see
"Build-time selection", below.

A sensor module (`sensor_tof`, `sensor_ldc`) never includes `usb_cdc.h` or `proto_reply.h`
directly. It talks only to `sensor_core`, which talks to `device_proto` on its behalf.
`sensor_core` does not depend on `board`: a `sensor_board_binding_t` (an I2C bus handle and an
address) arrives already filled in from whichever device assembly calls
`sensor_core_register()`.

## Where the channel registry lives, and why

`device_proto` owns the channel registry, not `sensor_core`. `GET`, `SET`, and `CAPS` are
generic commands: they read and write named values, with no sensor-specific meaning. Putting
the registry in `device_proto` lets those three commands work whether a channel came from a
sensor module, or from a device assembly's own code (`led0`, `uptime_us`).

`sensor_core` keeps a second, separate registry: configuration keys, for the `SCAPS` and `SCFG`
commands. A configuration key is not a channel. It has a type, a range, a default, and a
read-back verification step that a channel does not have. See
`docs/09-sensor-configuration.md`.

## Build-time selection

Three choices are made at build time, each by one make variable, each turned into a C
preprocessor define:

| Variable | Default | Becomes | Read by |
|---|---|---|---|
| `DEVICE` | `atoms3_lite` | Which `main/devices/<DEVICE>/` directory `main/CMakeLists.txt` builds. | The root `CMakeLists.txt` and `main/CMakeLists.txt`. |
| `BOARD` | `atoms3_lite` | `-DBOARD_<BOARD>=1` | `components/board/src/board.c`, to pick one profile header. |
| `SENSOR` | `tof` | `-DSENSOR_<SENSOR>=1`, and whether `main` requires `sensor_<SENSOR>` at all | `main/devices/<DEVICE>/app_main.c`'s `#if` guards. |

`SENSOR` takes one value at a time: `tof`, `ldc`, or `none`. Building more than one sensor
module into the same firmware image is out of scope for this version; see
`docs/00-overview.md`.

`SENSOR=none` means no sensor module is required by `main` at all. ESP-IDF's `MINIMAL_BUILD`
property, set in the root `CMakeLists.txt`, means only components reachable from `main`'s own
requirements are built -- an unselected sensor module is not compiled, not linked, and adds
nothing to the firmware's size. See `docs/14-build-and-flash.md`.

## Extending the command set

`device_proto` ships 15 built-in verbs (`docs/07-command-reference.md`). `sensor_core` adds
four more (`SENS`, `SCAPS`, `SCFG`, `CAL`) by calling `proto_cmd_register_verb()`, a function
`device_proto` exposes for exactly this. `device_proto`'s dispatcher does not know, and does
not need to know, that these four verbs come from a different component. A future addition
(a second kind of sensor, a new device-level feature) can add its own verb the same way, with
no change to `device_proto` itself.
