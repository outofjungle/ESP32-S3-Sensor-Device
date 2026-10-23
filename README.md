# ESP32-S3 Sensor Device

A sensor library and USB serial protocol for the ESP32-S3 chip, with firmware built on it: an
M5Stack AtomS3 Lite, a Grove sensor on its Grove port, and a browser control panel.

A Grove sensor module and a board profile each plug into the same core at build time, selected
by make variables — adding one costs no change to the core. Two sensor modules exist, one
built in at a time: a VL53L1X time-of-flight distance sensor, and an LDC1612 2-channel
inductive sensor. The first board is the AtomS3 Lite, used over its one Grove port during
development. The eventual target is a custom PCB carrying an ESP32-S3 and several sensor
ports.

Full documentation lives in [`docs/`](docs/00-overview.md) — start at
[`docs/00-overview.md`](docs/00-overview.md).

## What you need

| Item | Notes |
|---|---|
| M5Stack AtomS3 Lite | The default board. |
| M5Stack ToF4M Grove unit (VL53L1X), or Seeed Grove 2-Channel Inductive Sensor (LDC1612) | Connects to the AtomS3 Lite's Grove port. Pick one; `SENSOR` selects which the firmware expects. |
| USB-C cable | Data-capable, not charge-only. |
| Docker | For the build. |
| A Chromium-based browser (Chrome or Edge) | For the control panel — it uses the Web Serial API. |

## Quickstart

```sh
make image-build      # build the Docker image, once
make build            # build firmware (DEVICE=atoms3_lite BOARD=atoms3_lite SENSOR=tof, by default)
# enter firmware download mode -- see docs/02-board-atoms3-lite.md
make flash
make web              # open the control panel launcher in Chrome, or open web/index.html with no hardware
```

See [`docs/14-build-and-flash.md`](docs/14-build-and-flash.md) for every `make` target, and
the `DEVICE`/`BOARD`/`SENSOR` variables that select what gets built.

## Project layout

```
components/
  device_proto/    the wire protocol, transport, and the channel registry
  sensor_core/      the sensor library: the driver interface and its registries
  sensor_tof/       the VL53L1X sensor module
  vl53l1x_uld/      ST's vendored Ultra Lite Driver, plus this project's I2C transport for it
  sensor_ldc/       the LDC1612 sensor module
  board/            one pin/bus profile header per board
  status_led/        the onboard RGB LED's status state machine
  tinycbor/          vendored CBOR encoder/parser
main/
  devices/atoms3_lite/   the one device assembly in this version
docs/                 full documentation, numbered by reading order
web/
  index.html             the control panel launcher
  vl53l1x.html           the VL53L1X control panel
  ldc1612.html           the LDC1612 control panel
  common/                files the panels share: the wire protocol, shared UI, the mock device
```

Adding a third sensor module: [`docs/15-adding-a-sensor.md`](docs/15-adding-a-sensor.md).
Adding a second board or device assembly:
[`docs/16-adding-a-device.md`](docs/16-adding-a-device.md).

## Licenses

Two components are vendored from upstream projects, under their own licenses:

| Component | Upstream | License |
|---|---|---|
| `components/tinycbor` | [intel/tinycbor](https://github.com/intel/tinycbor) | MIT |
| `components/vl53l1x_uld` | ST's VL53L1X Ultra Lite Driver, via [Saleca/vl53l1x_uld_esp_wrapper](https://github.com/Saleca/vl53l1x_uld_esp_wrapper) | BSD-3-Clause |

See each component's own `VENDORED.md` for the exact version and commit vendored.
