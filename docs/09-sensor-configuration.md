# Sensor Configuration

This document describes the `SENS`, `SCAPS`, `SCFG`, and `CAL` commands, and each sensor
module's own configuration keys. See `docs/00-overview.md` for term definitions, and
`docs/07-command-reference.md` for the device-level `CFG` command, which this document does
not cover.

## Why sensor settings are on the wire

A time-of-flight sensor's settings change what it measures: its range, its field of view, and
what counts as "present". A host needs to see these settings, change them, and know the device
actually applied the change. `SCFG` always reads the value back from the chip after writing it,
and reports a mismatch as an error, rather than trusting that a write succeeded.

## SENS

No arguments. One `[3, seq, "SENS", name, type, bus, addr, state]` row per registered sensor
instance, then `[1, seq, "SENS", count]`.

| Column | Meaning |
|---|---|
| `name` | The instance name, e.g. `"tof0"`. |
| `type` | The sensor module's type name, e.g. `"vl53l1x"`. |
| `bus` | The bus it is reached on. Always `"i2c"` in this version. |
| `addr` | The bus address, e.g. `0x29`. |
| `state` | `"ok"`, `"busy"`, `"init_failed"`, `"start_failed"`, or `"fault"`. See below. |

| `state` value | Meaning |
|---|---|
| `"ok"` | Normal operation. |
| `"busy"` | A `CAL` action owns this instance's chip right now. `SCFG` and `GET` on its channels still work; a second `CAL` or a `SCFG` write does not. |
| `"init_failed"` | The chip was found, but one-time setup failed. |
| `"start_failed"` | Setup succeeded, but the chip did not start sampling. |
| `"fault"` | The sensor's probe step failed, or the sensor module later gave up trusting its own data. Its channels still exist, and report `null`, until this is fixed. |

`probe()` runs once, at registration, which happens once at boot. A sensor that is
disconnected and reconnected while the device is running stays faulted until the device
restarts (`RST`); this version does not re-probe a faulted instance on its own. See
`docs/20-state-machines.md` for the full instance lifecycle, as a diagram.

## SCAPS

No arguments, or one argument: an instance name to filter by (e.g. `"tof0"`). One
`[3, seq, "SCAPS", key, type, min, max, default, unit, enum_values]` row per configuration key,
then `[1, seq, "SCAPS", count]`.

| Column | Meaning |
|---|---|
| `key` | The key's full, dotted name, e.g. `"tof0.dist_mode"`. |
| `type` | `"uint"`, `"int"`, `"bool"`, or `"enum"`. |
| `min`, `max` | The valid range. For `"enum"`, the valid index range (always starting at 0). |
| `default` | The key's default value, in its own type. |
| `unit` | A text unit (e.g. `"mm"`), or `null` if the key has none. |
| `enum_values` | For `"enum"`, a comma-joined list of the valid text values (e.g. `"SHORT,LONG"`). `null` for every other type. |

A web client builds its whole configuration form from this command, so the form and the
firmware cannot drift apart. See `docs/17-web-control-panel.md`.

## SCFG

One argument (a key) reads it; two arguments (a key and a value) write it.

- Read: replies `[1, seq, "SCFG", value]`, read live from the chip.
- Write: validates the value against the key's declared range, writes it, reads it back, and
  compares. A match replies `[1, seq, "SCFG", value]` with the value just read back. A mismatch
  replies `[2, seq, "SCFG", "EVERIFY"]` -- the device wrote a value, but the chip now holds
  something else. Read the key again to see what it actually holds.

`SCFG` fails `ESTATE` if the named instance's sensor is in the `"fault"` state (see `SENS`).

## CAL

Two arguments: a key naming an instance and an action (e.g. `"tof0.offset"`), and a target
distance in millimeters. Replies `[1, seq, "CAL", result]`. Fails `ESTATE` while streaming, or
while the named instance is at fault. Fails `ERANGE` for a target outside the sensor module's safe range
(or outside 32 bits), and `ENOCH` for an action name the sensor module does not know. See "Calibration", below.

## The VL53L1X's configuration keys

| Key | Type | Range | Default | Unit |
|---|---|---|---|---|
| `tof0.dist_mode` | enum | `SHORT`, `LONG` | `LONG` | -- |
| `tof0.budget_ms` | uint | 15, 20, 33, 50, 100, 200, or 500 | 50 | ms |
| `tof0.intermeas_ms` | uint | 20 to 5000, and at least `budget_ms` | 100 | ms |
| `tof0.roi_x` | uint | 4 to 16 | 16 | -- |
| `tof0.roi_y` | uint | 4 to 16 | 16 | -- |
| `tof0.roi_center` | uint | 0 to 255 | 199 | -- |
| `tof0.offset_mm` | int | -1023 to 1023 | 0 | mm |
| `tof0.xtalk_cps` | uint | 0 to 65535 | 0 | cps |
| `tof0.sigma_mm` | uint | 1 to 16383 | 15 | mm |
| `tof0.signal_kcps` | uint | 1 to 65535 | 1024 | kcps |
| `tof0.window` | enum | `OFF`, `BELOW`, `ABOVE`, `OUT`, `IN` | `IN` | -- |
| `tof0.thresh_low_mm` | uint | 0 to 4000 | 50 | mm |
| `tof0.thresh_high_mm` | uint | 0 to 4000, and at least `thresh_low_mm` | 1500 | mm |
| `tof0.int_polarity` | enum | `HIGH`, `LOW` | `HIGH` | -- |

The chip stores some values in a reduced form. It stores `offset_mm` in 11 bits, so -1024 would read
back as +1024; the range starts at -1023. It stores `sigma_mm` shifted left by 2 bits, so it
refuses a value above 16383. It stores `xtalk_cps` as `(cps * 512) / 1000` and `signal_kcps` divided
by 8, so the chip holds a rounded value. After a write to either of those two keys, `SCFG` reports the
requested value if the chip holds exactly the rounded form of it. It reports the chip's own value if
something else (a `CAL` run) changed the register since.

`tof0.roi_x` or `tof0.roi_y` above 10 makes the chip set `tof0.roi_center` to 199. Write `roi_center`
after `roi_x` and `roi_y` if a different centre is needed.

`tof0.budget_ms` of 15 is valid only while `tof0.dist_mode` is `SHORT`; writing 15 while in
`LONG` mode fails `ERANGE`.

### A key the chip's own ULD header deprecates

ST's reference driver's `SetDistanceThreshold` function used to accept a fourth argument,
"interrupt on no target", that changed whether an empty scene counted as present. The vendored
driver's own header states this argument "is no longer used -- just use 0" in this driver
version. This project does not expose it as a configuration key, and the firmware always
passes 0.

## Ordering rules for the VL53L1X, handled inside the sensor module

ST's user manual states two rules: `SetDistanceMode` must be called before
`SetTimingBudgetInMs`, and the inter-measurement period must not be shorter than the timing
budget. A host does not need to send keys in any particular order to satisfy these. Every
write to `dist_mode`, `budget_ms`, or `intermeas_ms` re-reads and re-asserts the other related
settings before writing the one actually requested, so the chip stays consistent regardless of
which order a host changes them in. The same self-consistency applies to the region of
interest (`roi_x`/`roi_y`, written together through one `SetROI` call) and to the detection
window (`thresh_low_mm`/`thresh_high_mm`/`window`, written together through one
`SetDistanceThreshold` call).

## Presence detection on the VL53L1X

`tof0.present` reflects the VL53L1X's own window-detection feature, set by `window`,
`thresh_low_mm`, and `thresh_high_mm`. With a window programmed, the chip's interrupt condition
means "the window condition holds", not "a new sample exists" -- so the sensor module reads
the measurement result on every poll regardless of that condition, to keep `tof0.dist`
updating whether or not anything is present. When a poll produces the same distance and status
as the previous one, the sample carries the `STALE` flag; see
`docs/08-channel-model-and-types.md`.

Setting `window` to `OFF` writes the chip's register back to its reset value, restoring plain
"new measurement ready" behavior with no window condition. `tof0.present` then reports `null`,
since there is no window condition for it to report.

### A register detail worth knowing

Reading back `window`'s current value is not a single ST API call. ST's own
`VL53L1X_GetDistanceThresholdWindow()` cannot tell `OFF` (this project's own convention, not a
chip-defined value) from `BELOW` with particular bits, so this project reads the raw register
and checks a specific bit the chip reserves for its reset value, directly. Setting
`thresh_low_mm` or `thresh_high_mm` while `window` is `OFF` also needs care: ST's own
`SetDistanceThreshold` call always turns window detection on as a side effect, so a plain
threshold edit while `OFF` writes the two threshold registers directly instead, leaving
`window` untouched.

## Calibration (VL53L1X only)

| Action | Target distance | Procedure |
|---|---|---|
| `tof0.offset` | 100 mm, against a 17%-grey target (ST's recommendation) | Averages 50 measurements, computes an offset, and writes it. Returns the offset found, in millimeters. |
| `tof0.xtalk` | The sensor's own "inflection point" distance -- where readings start to under-range because of light reflected from a cover glass | Averages 50 measurements, computes a crosstalk value, and writes it. Returns the value found, in counts per second (cps). |

Both actions take several seconds. The poll task skips its cycle for an instance while a
calibration action runs on it, so the two do not race over the same I2C transactions.

`sensor_ldc` defines no action. `CAL ldc0.<anything>` fails `ENOCH`.

## The LDC1612's configuration keys

22 keys, named `ldc0.ch0_*` / `ldc0.ch1_*` for a per-channel setting, or `ldc0.*` with no
`chN_` part for a chip-wide setting. Most keys hold a raw register value, not an engineering
unit, so that a write and the read-back that follows it land on the same integer. `SCFG`
replies `EVERIFY` otherwise; see `docs/15-adding-a-sensor.md`. See `docs/19-sensor-ldc1612.md`
for what each register value means physically.

| Key | Type | Range | Default | Register |
|---|---|---|---|---|
| `ldc0.ch0_rcount` | uint | 4 to 65535 | 1350 | `RCOUNT0`, `0x08` |
| `ldc0.ch1_rcount` | uint | 4 to 65535 | 1350 | `RCOUNT1`, `0x09` |
| `ldc0.ch0_settlecount` | uint | 2 to 65535 | 30 | `SETTLECOUNT0`, `0x10` |
| `ldc0.ch1_settlecount` | uint | 2 to 65535 | 30 | `SETTLECOUNT1`, `0x11` |
| `ldc0.ch0_fin_div` | uint | 1 to 15 | 1 | `CLOCK_DIVIDERS0[15:12]` |
| `ldc0.ch1_fin_div` | uint | 1 to 15 | 1 | `CLOCK_DIVIDERS1[15:12]` |
| `ldc0.ch0_fref_div` | uint | 1 to 1023 | 2 | `CLOCK_DIVIDERS0[9:0]` |
| `ldc0.ch1_fref_div` | uint | 1 to 1023 | 2 | `CLOCK_DIVIDERS1[9:0]` |
| `ldc0.ch0_idrive` | uint | 0 to 31 | 20 | `DRIVE_CURRENT0[15:11]` |
| `ldc0.ch1_idrive` | uint | 0 to 31 | 20 | `DRIVE_CURRENT1[15:11]` |
| `ldc0.ch0_window` | enum | `OFF`, `BELOW`, `ABOVE`, `OUT`, `IN` | `OFF` | None. Held in firmware. |
| `ldc0.ch1_window` | enum | `OFF`, `BELOW`, `ABOVE`, `OUT`, `IN` | `OFF` | None. Held in firmware. |
| `ldc0.ch0_thresh_low` | uint | 0 to 268435455, and at most `ch0_thresh_high` | 0 | None. Held in firmware. |
| `ldc0.ch1_thresh_low` | uint | 0 to 268435455, and at most `ch1_thresh_high` | 0 | None. Held in firmware. |
| `ldc0.ch0_thresh_high` | uint | 0 to 268435455 | 268435455 | None. Held in firmware. |
| `ldc0.ch1_thresh_high` | uint | 0 to 268435455 | 268435455 | None. Held in firmware. |
| `ldc0.active_chan` | enum | `CH0`, `CH1` | `CH0` | `CONFIG[15:14]` |
| `ldc0.autoscan` | bool | -- | `true` | `MUX_CONFIG[15]` |
| `ldc0.deglitch` | enum | `1MHZ`, `3P3MHZ`, `10MHZ`, `33MHZ` | `3P3MHZ` | `MUX_CONFIG[2:0]` |
| `ldc0.ref_clk_src` | enum | `INTERNAL`, `CLKIN` | `CLKIN` | `CONFIG[9]` |
| `ldc0.rp_override` | bool | -- | `true` | `CONFIG[12]` |
| `ldc0.high_current` | bool | -- | `false` | `CONFIG[6]` |

The threshold values are in raw 28-bit count units, matching `ldc0.chN_raw` -- not hertz --
because the firmware compares the raw count directly; see "`cfg_get()` and keys with no chip
register", below.

The defaults match the Seeed reference configuration: `CONFIG` `0x1601`, `MUX_CONFIG`
`0x820C`, both `RCOUNT` registers `0x0546`, both `SETTLECOUNT` registers 30, and both
`DRIVE_CURRENT` registers `0xA000`.

## Threshold order and poll interval for the LDC1612

A write that would leave `chN_thresh_low` above `chN_thresh_high` fails `ERANGE`, as it does for
`tof0`. To move the whole range up, write the high threshold first. To move it down, write the low
threshold first. `LOAD` and boot apply each saved key once, then apply a key that failed `ERANGE`
a second time, so the order of the saved keys does not matter.

A channel's `present` channel reports `false`, never an old `true`, when the channel is not
converting or its last poll failed. The poll interval is at least 10 ms and at most 1000 ms, so two
in-range settings cannot stop a channel updating for tens of seconds.

## Ordering rules for the LDC1612, handled inside the sensor module

Two pairs of keys interact. `sensor_ldc`'s `cfg_set()` re-reads and re-asserts the other key
in each pair on every write, the same pattern `docs/15-adding-a-sensor.md` describes for the
VL53L1X's `dist_mode` and `budget_ms`:

- **`autoscan` and `active_chan`.** With `autoscan` true, the chip converts both channels in
  turn and `active_chan` has no effect. With `autoscan` false, only `active_chan` converts,
  and the other channel's `raw` channel reports `null`.
- **`high_current` and `autoscan`.** The datasheet states `high_current` is not supported
  while the chip converts more than one channel. Writing `ldc0.high_current true` while
  `ldc0.autoscan` is `true` fails `ERANGE`, and so does the reverse: writing `ldc0.autoscan
  true` while `ldc0.high_current` is already `true`.

Writes to `RCOUNT`, `SETTLECOUNT`, `CLOCK_DIVIDERS`, or `DRIVE_CURRENT` only take effect
cleanly while the chip is not converting. `cfg_set()` sets `CONFIG.SLEEP_MODE_EN`, writes the
register, clears `SLEEP_MODE_EN`, and reads back -- one self-contained sequence per key, so
key order does not matter.

## Presence detection on the LDC1612

The LDC1612 has no window-detection hardware, unlike the VL53L1X. `ldc0.ch0_present` and
`ldc0.ch1_present` are computed in firmware: on every poll, `sensor_ldc` compares the
channel's raw count against `chN_thresh_low` and `chN_thresh_high`, the same way the device
reports `BELOW`, `ABOVE`, `OUT`, or `IN` that the VL53L1X's own `window` enum names. Setting
`chN_window` to `OFF` makes the matching `present` channel report `null`.

## `cfg_get()` and keys with no chip register

`docs/15-adding-a-sensor.md` states that `cfg_get()` always reads the chip, never a cached
copy. The VL53L1X's window detection runs in hardware, so `tof0.window` and its thresholds
are chip registers, and that rule holds without exception.

The LDC1612 has no threshold hardware. `ldc0.ch0_window`, `ch0_thresh_low`, `ch0_thresh_high`,
and their `ch1_` equivalents have no chip register behind them. For these six keys, the value
`sensor_ldc` holds in its own context **is** the authoritative value: `cfg_get()` returns it
directly, and `SAVE` writes it to flash like every other key. The other 16 LDC1612 keys read
their register, as the rule states.

## Persistence

Every sensor instance's configuration is saved to flash in its own record, separate from the
device's `CFG` settings, under the `SAVE` and `LOAD` commands. See
`docs/07-command-reference.md`. A record carries a version byte; a version mismatch (from a
firmware update that changed a sensor module's key list) is treated as nothing saved, falling
back to each key's default, rather than misreading old data under a new layout.
