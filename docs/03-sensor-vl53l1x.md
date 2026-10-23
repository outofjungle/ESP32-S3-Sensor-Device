# Sensor: VL53L1X Time-of-Flight

This document describes the VL53L1X chip, as used by the `sensor_tof` module. See
`docs/00-overview.md` for term definitions, and `docs/09-sensor-configuration.md` for the
configuration keys this module exposes.

## The chip

| Item | Value |
|---|---|
| Chip | VL53L1CXV0FY (the VL53L1X family) |
| Measurement principle | Time of Flight (ToF): timing a pulse of infrared light to and from a target |
| I2C address (default) | `0x29` |
| Range | 40 millimeters (mm) to 4000 mm |
| Field of view | 15 to 27 degrees, set by the region of interest. See below. |
| Datasheet | `docs/VL53L1CXV0FY-datasheet.pdf`, in this repository |

## The Grove unit

This project's hardware is the M5Stack ToF4M Grove unit, which carries the VL53L1X chip.

| Grove wire | Signal |
|---|---|
| Black | Ground (GND) |
| Red | 5V |
| Yellow | I2C data (SDA) |
| White | I2C clock (SCL) |

The unit does not expose the chip's XSHUT (shutdown) or GPIO1 (interrupt) pins on its Grove
connector. Two consequences:

1. This firmware cannot hardware-reset the sensor by driving XSHUT. A device power cycle is
   the only reset available.
2. This firmware cannot wait for a hardware interrupt. `sensor_tof.c`'s poll task reads the
   chip's interrupt status over I2C instead, on its own schedule. See
   `docs/09-sensor-configuration.md`, "Presence detection".

## The Single Photon Avalanche Diode (SPAD) array and the region of interest

The chip measures with a 16-by-16 grid of Single Photon Avalanche Diodes (SPADs). The region
of interest (ROI) selects which SPADs are active, as a width and a height (4 to 16 each) and a
center SPAD number (0 to 255). A smaller ROI narrows the field of view and can reject a
reflective object at the edge of the full array; the sensor's 15-to-27-degree field of view
range corresponds to the full 16-by-16 array down to the smallest usable ROI. `roi_x`,
`roi_y`, and `roi_center` are the configuration keys for this; see
`docs/09-sensor-configuration.md`.

## Range status codes

Every measurement carries a range status. `sensor_tof` exposes it as the `status` channel
(`tof0.status`). ST's VL53L1X API user manual documents these five values:

| Code | Meaning |
|---|---|
| 0 | No error. The measurement is valid. |
| 1 | Sigma failure. The measurement's repeatability (standard deviation) is worse than the sigma threshold allows. See `sigma_mm` in `docs/09-sensor-configuration.md`. |
| 2 | Signal failure. The returned signal is too weak: the target is too far, not reflective enough, or too small. See `signal_kcps`. |
| 4 | Out of bounds. The chip is ranging in a zone it is not designed for, typically near its maximum distance. |
| 7 | Wraparound. The target is very reflective, and the measured distance may have wrapped past the chip's unambiguous range. |

Other numeric values can appear (the chip's full internal register has more states than these
five), but ST's user manual documents only these five for application use. `tof0.dist`
reports CBOR `null` whenever `status` is not 0, rather than a number that may not be
trustworthy. The value 255, specifically, means no measurement has completed yet.

## What one measurement reports

One call to the ULD's `VL53L1X_GetResult()` returns four values in a single I2C transaction,
all exposed as channels:

| Channel | ULD field | Meaning |
|---|---|---|
| `dist` | `Distance` | Distance in millimeters. |
| `status` | `Status` | The range status code, above. |
| `signal` | `SigPerSPAD` | Signal rate, in kilo counts per second (kcps) per SPAD. |
| `ambient` | `Ambient` | Ambient light rate, in kcps. |
| `spads` | `NumSPADs` | Number of SPADs enabled for this measurement. |

See `docs/08-channel-model-and-types.md` for the full channel table, including `present`.
