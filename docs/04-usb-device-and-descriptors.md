# USB Device and Descriptors

This document describes how the device presents itself on the USB bus. See
`docs/00-overview.md` for term definitions.

## USB device class

The device uses the standard USB Communications Device Class (CDC), the same class a USB
modem or a USB-to-serial adapter uses. The operating system's own serial driver claims it on
every major platform, with no custom driver needed.

## Why a second port, and why it is off by default

The device presents two CDC ports. CDC0 carries the command, response, and sample protocol
described in `docs/05-message-model.md`. CDC1, when enabled, carries plain-text log output.

CDC1 is off by default, so the device normally presents one port, and a host does not need to
guess which of two ports to open. Holding the board's button down through power-on or reset
enables it. See `docs/02-board-atoms3-lite.md`.

## Device descriptor

| Field | Value | Meaning |
|---|---|---|
| Vendor ID (VID) | Espressif's VID | Set by `CONFIG_TINYUSB_DESC_USE_ESPRESSIF_VID=y`. |
| Product ID (PID) | `0x8002`, a placeholder | Not yet registered through `github.com/espressif/usb-pids`. Deliberately different from the sibling M5Atom-Serial-Switch-Sensor project's `0x8001`. |
| Manufacturer string | "ESP32-S3 Sensor Device Project" | |
| Product string | "ESP32-S3 Sensor Device" | |

## String descriptors

| Index | Content |
|---|---|
| 0 | Language ID (US English) |
| 1 | Manufacturer string |
| 2 | Product string |
| 3 | Serial number, built from the chip's eFuse MAC address at boot |
| 4 | CDC0 interface name: "`<product name>` Data" |
| 5 | CDC1 interface name: "`<product name>` Debug" (present only when CDC1 is enabled) |

## Serial number

Built once at boot, from the chip's factory-programmed MAC address, as 12 hexadecimal
characters. Two different devices never share a serial number.

## Configuration and interfaces

```
Configuration
+-- Interface 0: CDC0 Communications (notification endpoint)
+-- Interface 1: CDC0 Data (bulk IN/OUT, 64-byte packets)
+-- Interface 2: CDC1 Communications   (present only when CDC1 is enabled)
+-- Interface 3: CDC1 Data              (present only when CDC1 is enabled)
```

Two complete, independently valid configuration descriptors exist in `usb_cdc.c`: one with
CDC1, one without. The device picks one at `tinyusb_driver_install()` time, based on the
button's state at boot -- never a configuration descriptor truncated at runtime.

## Endpoint count and packet size

Four endpoints with CDC1 enabled (two notification, two data), two without. Every data
endpoint uses 64-byte packets: the ESP32-S3's USB peripheral runs at Full Speed only, and 64
bytes is the maximum packet size Full Speed allows.

## Power

The device is bus-powered, drawing up to 100 milliamps (mA), declared in the configuration
descriptor.
