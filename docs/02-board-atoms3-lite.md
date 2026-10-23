# Board: M5Stack AtomS3 Lite

This document describes the default board profile. See `docs/00-overview.md` for term
definitions, and `docs/16-adding-a-device.md` to add a second board profile.

## Main chip

| Item | Value |
|---|---|
| Chip | ESP32-S3FN8 |
| CPU | Two Xtensa LX7 cores, 240 MHz |
| Flash memory | 8 megabytes (MB), built into the chip package |
| Extra Random Access Memory (PSRAM) | None |
| USB connector | USB Type-C |

## Pin table

| Pin | Function | Notes |
|---|---|---|
| G41 | Push button | Pulled high when not pressed. Reads low when pressed. |
| G35 | RGB LED data line | Chip type WS2812C-2020. One LED. |
| G1 | Grove port, SCL (white wire) | Also usable as General Purpose Input/Output (GPIO) or Universal Asynchronous Receiver/Transmitter (UART). |
| G2 | Grove port, SDA (yellow wire) | Also usable as GPIO or UART. |

The Grove connector's remaining two pins, 5V and GND, carry no GPIO number.

## Board profile

`components/board/include/board_atoms3_lite.h` states this table in code, as one
`board_profile_t` value: the Grove port's SDA and SCL pins and clock speed, the LED pin, and
the button pin and its active level. No other file in this project names a GPIO number for
this board; a sensor module or `status_led.c` asks `board_get_profile()` or
`board_i2c_bus_init()` instead. See `docs/01-architecture.md`.

## USB wiring

USB-C on this board connects directly to the ESP32-S3's USB-OTG peripheral, not to its
USB-Serial-JTAG peripheral. The chip has one USB Physical Layer (PHY) that serves one or the
other, not both. Two consequences:

1. The IDF console goes nowhere (`CONFIG_ESP_CONSOLE_NONE=y` in `sdkconfig.defaults`). This
   firmware's log output goes to CDC1 instead, when CDC1 is enabled. See
   `docs/12-flow-control-and-backpressure.md`.
2. Once this firmware is running, the chip's automatic download-mode entry (over
   USB-Serial-JTAG) is not available. Use the manual procedure below.

## Enabling the debug (log) port

Holding the button (G41) down through power-on or reset brings up a second USB serial port,
CDC1, carrying this firmware's log output. CDC0, the command and sample port, is always
present. See `docs/04-usb-device-and-descriptors.md`.

## Entering firmware download mode

1. Unplug the USB-C cable.
2. Hold the side button (G41) down.
3. Plug the USB-C cable back in, while still holding the button.
4. Wait about 2 seconds, until the LED's boot color (if any) changes or the host's device list
   shows a new download-mode port.
5. Release the button.
6. Run `make flash`.
