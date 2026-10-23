# Overview

This document states this project's goal and scope. It defines the terms every other document
in `docs/` uses. Read this document first.

## Goal

This project builds two things. The first is a library for the ESP32-S3 chip. The library lets
a developer add a Grove sensor to a device, and expose it over USB, with no change to the
library itself. The second is firmware that uses the library: an M5Stack AtomS3 Lite board,
one Grove sensor on its Grove port, and a web browser control panel.

The library and the firmware are proof of concept (PoC) work. The eventual target is a custom
printed circuit board (PCB), carrying an ESP32-S3 chip and several Grove ports, each with its
own sensor. The AtomS3 Lite board is a development board. It has one Grove port, so this PoC
builds and tests one sensor module at a time, chosen at build time.

## Scope

- A USB serial protocol: commands, responses, and a stream of samples.
- A sensor library: a fixed interface a sensor module implements, and a core that turns any
  module meeting that interface into channels and configuration keys on the wire.
- Two sensor modules, one built in at a time: the VL53L1X time-of-flight distance sensor, and
  the LDC1612 2-channel inductive sensor.
- One board profile: the M5Stack AtomS3 Lite.
- Device status shown on the onboard RGB LED.
- A web browser control panel, using the Web Serial API, with one page per sensor module.

## Out of scope

- Wi-Fi, Bluetooth, or any network protocol.
- Over-the-air (OTA) firmware updates.
- More than one sensor module built into the same firmware at once, in this version. The
  library's interface does not block this; the single `SENSOR` build variable does, and the
  AtomS3 Lite's one Grove port means only one sensor is attached at a time regardless.
- A second board profile. `docs/16-adding-a-device.md` describes how to add one.

## Terms

| Term | Meaning |
|---|---|
| Host | The computer that connects to the device by USB. |
| Device | The board running this firmware: the M5Stack AtomS3 Lite, in this version. |
| Frame | One complete message sent between the host and the device. |
| Command | A frame that the host sends to ask the device to do something. |
| Response | A frame that the device sends to answer one command. |
| Sample | One reading from one channel, sent by the device without a matching command. |
| Channel | One named source of data, or one named control, on the device. Example: `tof0.dist`. |
| Instance | One sensor attached to the device, numbered by its board port, starting at 0. Example: `tof0`, the VL53L1X instance on port 0. |
| Sensor module | The code that implements one kind of sensor, behind a fixed interface. |
| Configuration key | One named, adjustable setting on a sensor instance. Example: `tof0.dist_mode`. |
| Sequence number | A number in a frame, from 0 to 65535. It links a response to its command. |
| Cyclic Redundancy Check (CRC) | A number computed from a frame. The reader uses it to detect a damaged frame. |
| Verb | The command name inside a frame. Example: `GET`, `PING`, `SCFG`. |
| Board | The physical circuit board a device assembly runs on, and its pin layout. |
| Device assembly | One `main/devices/<name>/` directory: which board, which sensor module, which instance names. |

## Document index

| Document | Covers |
|---|---|
| `01-architecture.md` | The components, the dependency direction, build-time selection. |
| `02-board-atoms3-lite.md` | The board: pins, USB wiring, and how to enter firmware download mode. |
| `03-sensor-vl53l1x.md` | The VL53L1X chip: range, field of view, and its range status codes. |
| `04-usb-device-and-descriptors.md` | How the device presents itself on the USB bus. |
| `05-message-model.md` | The shape of a frame: command, response, row, and sample. |
| `06-cbor-encoding.md` | CBOR, the CRC, and COBS -- how a frame becomes bytes on the wire. |
| `07-command-reference.md` | Every command verb. |
| `08-channel-model-and-types.md` | What a channel is, its types, and the dotted naming rule. |
| `09-sensor-configuration.md` | `SENS`, `SCAPS`, `SCFG`, `CAL`, and each sensor module's own keys. |
| `10-led-status.md` | The onboard LED's states, and the manual-override rule. |
| `11-error-codes.md` | Every error code, and the order the device checks for them. |
| `12-flow-control-and-backpressure.md` | How the device avoids blocking on a slow or absent host. |
| `13-host-integration.md` | Finding the device's ports, and reference host code. |
| `14-build-and-flash.md` | Docker build, host flash, and the `DEVICE`/`BOARD`/`SENSOR` build variables. |
| `15-adding-a-sensor.md` | The sensor module interface, step by step. |
| `16-adding-a-device.md` | Adding a board profile or a new device assembly. |
| `17-web-control-panel.md` | The control panel's structure, and its offline mock mode. |
| `18-test-plan.md` | The acceptance tests for this version. |
| `19-sensor-ldc1612.md` | The LDC1612 chip: register map, and raw count to frequency. A second per-sensor document, after `03-sensor-vl53l1x.md`. It appends its own number, rather than inserting next to `03`, to avoid renumbering every document that comes after it. A future sensor document appends the same way. |
| `20-state-machines.md` | Every state machine in the firmware, with a diagram for each. |

## Design choices and the reasons for them

| Area | Choice | Reason |
|---|---|---|
| Frame content | A CBOR array, described in `05-message-model.md` | CBOR carries the type of each value on the wire. A channel can send a number, text, or a flag, with no change to the surrounding frame design. |
| Sensor settings on the wire | Exposed, with read-back verification (`SCFG`) | A sensor's settings (for example, distance mode and timing for the VL53L1X, or drive current and conversion time for the LDC1612) change what it measures. A host needs to see and set them, and to know the device actually applied them. |
| Channel naming | A dotted prefix per instance (`tof0.dist`) | One flat namespace keeps every existing command (`GET`, `SET`, `CAPS`) unchanged. The dot still lets a host group channels by instance. |
| Sensor selection | A build-time choice (`make build SENSOR=tof` or `SENSOR=ldc`), not a runtime one | One Grove port on this board means one sensor is attached at a time. An unselected sensor module costs no flash and no build time, not merely a disabled one. |
| Presence detection | Done in hardware where the chip supports it (the VL53L1X's window-detection feature); done in firmware, from the raw reading, where it does not (the LDC1612) | A chip's own hardware feature answers "is something in range" with no extra firmware logic, where one exists. The LDC1612 has no equivalent register, so `sensor_ldc` compares each poll's raw count against the configured threshold itself. Either way, a host reads the same kind of `present` channel. |
| Browser access | The Web Serial API, not the WebUSB API | The device's two ports use the standard USB serial port class, already claimed by the operating system's own driver on every major platform. The Web Serial API is built to work through that driver; WebUSB is not. See `13-host-integration.md`. |

## Who reviews this

The user reviews every document in `docs/` before any firmware code is written. This is a
requirement for this project, stated at its start.
