# Web Control Panel

This document describes `web/`: a launcher page, one page per sensor module, and a set of
shared files under `web/common/`. No build step, no content delivery network, no network
requests at all. Every page opens directly from `file://`, or served by any plain web server.
See `docs/00-overview.md` for term definitions.

## Why more than one file

A single device runs one sensor module at a time (`SENSOR=tof` or `SENSOR=ldc`), but each
module has its own channels, its own charts, and its own configuration keys. One page cannot
show a distance chart and a two-channel inductance chart at once, and should not pretend to.
`web/` has one page per sensor module instead, reached from a launcher.

A separate host program is still not needed: each page's Web Serial API call opens the
device's CDC0 port directly from the browser tab. See `docs/13-host-integration.md`.

## Layout

| Path | Contents |
|---|---|
| `web/index.html` | The launcher. One card per sensor module, linking to its page, plus the mock-mode links for trying each page with no hardware. `make web` opens this file. |
| `web/vl53l1x.html` | The control panel for `SENSOR=tof`: the distance chart, the presence strip, the calibration card. |
| `web/ldc1612.html` | The control panel for `SENSOR=ldc`: one chart per channel, the presence badges, the coil inductance display. |
| `web/common/panel.css` | The shared stylesheet -- colors, cards, the LED swatches, the activity log. |
| `web/common/protocol.js` | The wire codecs (COBS, the CRC, a CBOR subset), the startup self-test, and `class Link`, the transport. Ported from the same algorithms the firmware uses. See `docs/06-cbor-encoding.md`. |
| `web/common/panel-ui.js` | Everything else every panel shares: preferences, the activity log, the status pill, `probeGrantedPorts()`, the connect and disconnect sequence, the `SCAPS`-built configuration card, and the LED card. |
| `web/common/mock-core.js` | `MockSerialPort`: the framing and the 15 sensor-agnostic verbs, answering real wire bytes through the same parser a real device's bytes go through. |

Each page loads the shared files with plain `<script src="common/protocol.js">` tags. These
are classic scripts, not ES modules: a browser's module loader and `fetch()` both refuse to
load from `file://`, but a classic `<script src>` tag does not. This keeps the "no build step,
opens from `file://`" rule intact across more than one file.

## The seam between shared and sensor-specific code

`panel-ui.js` exposes one entry point, `Panel.start()`. Each page supplies only what is
specific to its sensor module:

| Field | Meaning |
|---|---|
| `subscribe` | The channel names to `SUB` at connect time, e.g. `['tof0.dist', 'tof0.present']`. |
| `onSample(detail)` | Routes one `DATA` frame to this page's own chart and badge state. |
| `onPoll(getMap, statValues)` | Called every 2 seconds, with the `GET *` rows and the `STAT` reply, for this page's own health card. |
| `draw()` | Called once per animation frame while connected, to redraw this page's own charts. |
| `extraCards` | Any card beyond the shared ones, e.g. the VL53L1X calibration card. |
| `mockProfile` | The fake device this page's `?mock=1` mode presents. See "Mock mode", below. |

`panel-ui.js` itself builds the configuration card from `SCAPS`, with no sensor-specific code:
one card per sensor instance, one control per configuration key, from the key's type, range,
unit, and (for an enum) its valid text values. The same `panel-ui.js` renders a correct
configuration card for a sensor module it has never seen, because it never names one.

## `web/vl53l1x.html`

| Section | Contents |
|---|---|
| Distance chart | A log-scale area chart of `tof0.dist`, 40 mm to 4000 mm, with the configured threshold band shaded behind the trace. Gridlines at 50, 100, 200, 500, 1000, 2000, and 4000 millimeters. A `null` reading breaks the line rather than plotting as zero. Log-scaled because readings span close to two orders of magnitude, where a linear axis would flatten the near end into a few pixels. |
| Presence | A state badge and a timeline strip, from `tof0.present`. |
| Sensor health | Range status decoded to text, signal and ambient rates, SPAD count, stale-sample count. |
| Calibration | Offset and crosstalk, each with a target-distance input and a 15-second command timeout (calibration takes longer than any other command). |

Connect sequence: `PING` -> `ID` -> `STOP` (tolerating `ESTATE`, if it was already stopped) ->
`SENS` -> `CAPS` -> `SCAPS` -> `SUB tof0.dist` -> `SUB tof0.present` -> `START` -> `GET led0`
(to show the device's current color).

## `web/ldc1612.html`

| Section | Contents |
|---|---|
| Channel 0 and channel 1 charts | One chart per channel, linear and auto-ranging rather than log-scale, since a raw count sits in a narrow band near a baseline rather than spanning orders of magnitude. Each shows the live raw count and the frequency, in megahertz. A `null` reading breaks the line, as in the distance chart. |
| Presence | One badge and one timeline strip per channel, from `ldc0.ch0_present` and `ldc0.ch1_present`. |
| Sensor health | `ldc0.status` decoded to named bits, plus `ldc0.ch0_err` and `ldc0.ch1_err` decoded to `UR`/`OR`/`WD`/`AE` labels. |
| Coil | A host-side input for the coil's capacitance, default 100 picofarads (pF), not sent to the device. Used only to compute and show inductance beside each channel's frequency. See `docs/19-sensor-ldc1612.md`. |

No calibration card: `sensor_ldc` defines no `CAL` action.

Connect sequence: `PING` -> `ID` -> `STOP` (tolerating `ESTATE`) -> `SENS` -> `CAPS` ->
`SCAPS` -> `SUB ldc0.ch0_raw` -> `SUB ldc0.ch1_raw` -> `SUB ldc0.ch0_present` ->
`SUB ldc0.ch1_present` -> `START` -> `GET led0`.

## Mock mode

`?mock=1` runs a page against a fake device living entirely in JavaScript, built from that
page's own `mockProfile`, answering real wire bytes through the same parser a real device's
bytes go through -- not a UI-only stub. `web/vl53l1x.html`'s mock profile simulates the
VL53L1X well enough to exercise the chart, the presence badge, and the configuration round
trip: a triangle-wave distance, occasional `null` windows, and a `window`-aware presence
calculation. `web/ldc1612.html`'s mock profile simulates two channels as a baseline raw count
with slow drift, plus a step when a simulated target approaches, honoring the window keys so
the presence badges move. Add `&fault=crc`, `&fault=drop`, or `&fault=err` to either page to
inject the matching failure mode and confirm the page's error handling.

## Opening it

`make web` opens `web/index.html`, the launcher, in Chrome -- the Web Serial API these pages
need is not available in every browser; see `docs/13-host-integration.md`. See
`docs/14-build-and-flash.md`.
