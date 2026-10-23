# Channel Model and Types

This document describes what a channel is, its types, and the naming rule. See
`docs/00-overview.md` for term definitions.

## What a channel is

A channel is one named source of data, or one named control, on the device. `GET`, `SET`, and
`CAPS` work on any channel the same way, whether it belongs to the device itself (`led0`) or to
a sensor instance (`tof0.dist`).

## The dotted naming rule

A sensor instance's channels are named `<instance>.<channel>`, e.g. `tof0.dist`. This keeps one
flat namespace: `GET`, `SET`, and `CAPS` take a plain channel name, with no change needed to
support more than one sensor instance. The dot lets a host split the name to group channels by
instance in its own display, without the protocol itself knowing about that grouping. A
device's own channels (`led0`, `uptime_us`) are not dotted.

## Value types are native CBOR types

| Type name | CBOR type | Meaning |
|---|---|---|
| `bool` | Boolean | `true` or `false`. |
| `uint` | Unsigned integer | A non-negative whole number. |
| `bytes` | Byte string | A fixed-length sequence of bytes, e.g. `led0`'s 3-byte RGB value. |

## The "no reading" value

Any channel, of any declared type, may report CBOR `null` instead of a value. This means "no
reading right now" -- before the first measurement, or when the sensor's own range status says
the last measurement is not trustworthy. See `docs/03-sensor-vl53l1x.md`.

## Flags field

A `DATA` frame's last item is a flags bit field.

| Bit | Name | Meaning |
|---|---|---|
| 0 | `DROP` | A previous sample was dropped because the USB transmit buffer was full. |
| 1 | `STALE` | This sample's value is identical to the previous one for this channel -- the sensor has not produced a new reading since the last poll. |
| 2 | `SAT` | Reserved. Not set by any channel in this version. |

## Channels in this version

| Channel | Declared type | Access | Meaning |
|---|---|---|---|
| `led0` | `bytes`, exactly 3 | RW | Color of the onboard RGB LED: red, green, blue. Writing it switches `LED_MODE` to `MANUAL`. See `docs/10-led-status.md`. |
| `uptime_us` | `uint` | RO | Time in microseconds since the device started. |
| `tof0.dist` | `uint`, or `null` | RO | Distance in millimeters. `null` when `tof0.status` is not 0. |
| `tof0.present` | `bool`, or `null` | RO | The chip's window-detection result. `null` when `tof0.window` is `OFF`. |
| `tof0.status` | `uint` | RO | The VL53L1X range status code. See `docs/03-sensor-vl53l1x.md`. |
| `tof0.signal` | `uint` | RO | Signal rate, in kilo counts per second (kcps) per SPAD. |
| `tof0.ambient` | `uint` | RO | Ambient light rate, in kcps. |
| `tof0.spads` | `uint` | RO | Number of SPADs enabled for the last measurement. |
| `ldc0.ch0_raw` | `uint`, or `null` | RO | Channel 0's 28-bit conversion result. `null` before the first conversion, on an error, or when `autoscan` is off and this is not the active channel. See `docs/19-sensor-ldc1612.md`. |
| `ldc0.ch1_raw` | `uint`, or `null` | RO | Channel 1's 28-bit conversion result. Same `null` rules as `ldc0.ch0_raw`. |
| `ldc0.ch0_freq_hz` | `uint`, or `null` | RO | Channel 0's sensor frequency in hertz, computed from the raw count. `null` under the same rules as `ldc0.ch0_raw`. |
| `ldc0.ch1_freq_hz` | `uint`, or `null` | RO | Channel 1's sensor frequency in hertz. |
| `ldc0.ch0_present` | `bool`, or `null` | RO | The firmware's window result for channel 0. `null` when `ldc0.ch0_window` is `OFF`. |
| `ldc0.ch1_present` | `bool`, or `null` | RO | The firmware's window result for channel 1. `null` when `ldc0.ch1_window` is `OFF`. |
| `ldc0.ch0_err` | `uint` | RO | Channel 0's 4 error flags from `DATA0_MSB`, as a bit field. |
| `ldc0.ch1_err` | `uint` | RO | Channel 1's 4 error flags from `DATA1_MSB`, as a bit field. |
| `ldc0.status` | `uint` | RO | The LDC1612's `STATUS` register. |

`tof0.*` exists only when the firmware was built with `SENSOR=tof` (the default). `ldc0.*`
exists only when the firmware was built with `SENSOR=ldc`. See `docs/14-build-and-flash.md`.

## Adding a channel

A sensor module states its channels as a `sensor_channel_def_t` array; see
`docs/15-adding-a-sensor.md`. A device assembly adds its own channels (not a sensor's) by
calling `channel_registry_register()` directly, the way `main/devices/atoms3_lite/app_main.c`
does for `led0` and `uptime_us`.
