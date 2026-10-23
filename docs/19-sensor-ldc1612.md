# Sensor: LDC1612 2-Channel Inductive

This document describes the LDC1612 chip, as used by the `sensor_ldc` module. See
`docs/00-overview.md` for term definitions, and `docs/09-sensor-configuration.md` for the
configuration keys this module exposes.

This is the second sensor-specific document. The first is `docs/03-sensor-vl53l1x.md`. A new
sensor document appends its own number (this one is 19), rather than inserting next to the
first, to avoid renumbering every document after it.

## The chip

| Item | Value |
|---|---|
| Chip | LDC1612, Texas Instruments |
| Measurement principle | Inductance to Digital Converter (LDC): measures the oscillation frequency of an LC tank, formed by a coil and a capacitor |
| Channels | 2, named `ch0` and `ch1` |
| Conversion result | 28 bits per channel, proportional to the tank's oscillation frequency |
| I2C address | `0x2B` with the `ADDR` pin high, `0x2A` with it low |
| Sensor frequency range | 1 kilohertz (kHz) to 10 megahertz (MHz) |
| Manufacturer ID | `0x5449`, at register `0x7E` |
| Device ID | `0x3055`, at register `0x7F` |
| Datasheet | `docs/LDC1612-datasheet.pdf`, in this repository (Texas Instruments, document SNOSCY9A) |

## The Grove unit

This project's hardware is the Seeed Grove 2-Channel Inductive Sensor, which carries the
LDC1612 chip.

| Grove wire | Signal |
|---|---|
| Black | Ground (GND) |
| Red | 5V |
| Yellow | I2C data (SDA) |
| White | I2C clock (SCL) |

| Coil property | Value |
|---|---|
| Diameter | 16 millimeters (mm) |
| Turns | 20 |
| Board capacitance | 100 picofarads (pF) |
| Best detecting range | 15 mm |

The unit drives the chip's `CLKIN` pin from a 40 MHz external oscillator, rather than the
chip's own internal oscillator. This firmware sets `ref_clk_src` to `CLKIN` to match. The
chip's internal oscillator, used only if `ref_clk_src` is set to `INTERNAL`, runs at a
typical 43 MHz.

The unit does not expose the chip's `INTB` (interrupt) or `SD` (shutdown) pins on its Grove
connector. Two consequences:

1. This firmware cannot hardware-reset the sensor by driving `SD`. A device power cycle, or
   the chip's own `RESET_DEV` register, are the only resets available.
2. This firmware cannot wait for a hardware interrupt. `sensor_ldc.c`'s poll task reads the
   chip's `STATUS` register over I2C instead, on its own schedule.

## Register map

The firmware reads and writes the following 18 registers. It does not use the `OFFSET0`,
`OFFSET1`, or `ERROR_CONFIG` registers in this version. `OFFSET0`/`OFFSET1` exist to support a
future calibration action, which this version does not implement. `ERROR_CONFIG` routes
errors to the `INTB` pin, which the Grove unit does not expose; this firmware reads errors
from `DATAx_MSB` and `STATUS` instead.

| Address | Name | Purpose |
|---|---|---|
| `0x00` | `DATA0_MSB` | Channel 0 conversion result, high 12 bits, plus 4 error flags. |
| `0x01` | `DATA0_LSB` | Channel 0 conversion result, low 16 bits. Read after `DATA0_MSB`. |
| `0x02` | `DATA1_MSB` | Channel 1 conversion result, high 12 bits, plus 4 error flags. |
| `0x03` | `DATA1_LSB` | Channel 1 conversion result, low 16 bits. Read after `DATA1_MSB`. |
| `0x08` | `RCOUNT0` | Channel 0 reference count. Sets the conversion time. |
| `0x09` | `RCOUNT1` | Channel 1 reference count. Sets the conversion time. |
| `0x10` | `SETTLECOUNT0` | Channel 0 settle count. Sets the sensor activation settle time. |
| `0x11` | `SETTLECOUNT1` | Channel 1 settle count. Sets the sensor activation settle time. |
| `0x14` | `CLOCK_DIVIDERS0` | Channel 0 input divider and reference divider. |
| `0x15` | `CLOCK_DIVIDERS1` | Channel 1 input divider and reference divider. |
| `0x18` | `STATUS` | Device and per-channel status flags. Read-only. |
| `0x1A` | `CONFIG` | Active channel, sleep mode, reference clock source, and other chip-wide settings. |
| `0x1B` | `MUX_CONFIG` | Auto-scan enable, auto-scan sequence, deglitch filter bandwidth. |
| `0x1C` | `RESET_DEV` | Writing bit 15 resets the chip to its power-on state. |
| `0x1E` | `DRIVE_CURRENT0` | Channel 0 sensor drive current. |
| `0x1F` | `DRIVE_CURRENT1` | Channel 1 sensor drive current. |
| `0x7E` | `MANUFACTURER_ID` | Fixed value `0x5449`. Checked by `probe()`. |
| `0x7F` | `DEVICE_ID` | Fixed value `0x3055`. Checked by `probe()`. |

## What one measurement reports

`DATA0_MSB` and `DATA1_MSB` each carry 4 error flags in their top 4 bits, then the high 12
bits of a 28-bit conversion result:

| Bit | Name | Meaning |
|---|---|---|
| 15 | `ERR_UR` | Under-range: the sensor is not oscillating, or its frequency is too low. |
| 14 | `ERR_OR` | Over-range: the sensor's frequency is too high. |
| 13 | `ERR_WD` | Watchdog timeout: the sensor did not start oscillating in time. |
| 12 | `ERR_AE` | Amplitude error: the sensor's oscillation amplitude is outside the chip's working range. |
| 11:0 | `DATA[27:16]` | The high 12 bits of the 28-bit conversion result. |

The matching `DATAx_LSB` register holds bits 15 down to 0 of the same result.
`DATAx_LSB` must be read after `DATAx_MSB`, in the same order, to read one coherent
conversion. The full 28-bit result is `DATAx = DATAx_MSB[11:0] * 65536 + DATAx_LSB`.

A `DATAx` value of `0x0000000` means the channel is under-range. A value of `0xFFFFFFF`
means the channel is over-range.

## The `STATUS` register

| Bit | Name | Meaning |
|---|---|---|
| 15:14 | `ERR_CHAN` | Which channel caused the most recent flag or error: `00` channel 0, `01` channel 1. |
| 13 | `ERR_UR` | An under-range error happened on some channel since the last read of this register. |
| 12 | `ERR_OR` | An over-range error happened on some channel since the last read of this register. |
| 11 | `ERR_WD` | A watchdog timeout happened on some channel since the last read of this register. |
| 10 | `ERR_AHE` | An amplitude-high error happened on some channel since the last read of this register. |
| 9 | `ERR_ALE` | An amplitude-low error happened on some channel since the last read of this register. |
| 8 | `ERR_ZC` | A zero-count error happened on some channel since the last read of this register. |
| 6 | `DRDY` | A new conversion result is ready, for at least one active channel. |
| 3 | `UNREADCONV0` | Channel 0 has a conversion result that has not been read yet. |
| 2 | `UNREADCONV1` | Channel 1 has a conversion result that has not been read yet. |

Bits 7, 5, 4, 1, and 0 are reserved. This firmware exposes the whole 16-bit register as
`ldc0.status`, and decodes bits 15 through 8 into the two per-channel `ldc0.chN_err` channels.

## From raw count to frequency

| Quantity | Equation |
|---|---|
| Reference frequency | `f_ref = f_clk / FREF_DIVIDER` -- `f_clk` is 40 MHz (`CLKIN`) in this project. |
| Channel input frequency | `f_in = f_sensor / FIN_DIVIDER` |
| Sensor frequency, from a conversion result | `f_sensor = DATAx * f_ref / 2^28` (with `FIN_DIVIDER` at 1) |
| Conversion time | `t_c = (RCOUNT * 16 + 4) / f_ref` |
| Settle time | `t_s = (SETTLECOUNT * 16) / f_ref`, or `32 / f_ref` if `SETTLECOUNT` is 0 or 1 |

`ldc0.ch0_freq_hz` and `ldc0.ch1_freq_hz` report `f_sensor`, computed from the live `DATAx`,
`RCOUNT`, and `CLOCK_DIVIDERS` values on every poll.

## Inductance

This firmware does not report inductance. The coil's capacitance is a property of the coil,
not of the chip, so the firmware has no fixed value to compute it with. A host computes
inductance from the reported frequency and its own knowledge of the coil's capacitance:

```
L = 1 / ((2 * pi * f_sensor)^2 * C)
```

`C` is the coil's total capacitance (the board's 100 pF plus any parasitic capacitance). The
web control panel (`web/ldc1612.html`) takes `C` as a host-side input and shows the computed
inductance beside each channel's frequency. See `docs/17-web-control-panel.md`.
