# Host Integration

This document describes how a host finds the device's ports, and gives reference code. See
`docs/00-overview.md` for term definitions.

## Libraries used on each platform

| Layer | Microcontroller (C) | Host program (Python) | Web browser (JavaScript) |
|---|---|---|---|
| Concise Binary Object Representation (CBOR) | TinyCBOR | `cbor2` (from PyPI) | A small, vendored subset. About 100 lines. No dependency needed. |
| Consistent Overhead Byte Stuffing (COBS) | A small, vendored reference implementation. About 30 lines. | `cobs` (from PyPI) | A small, vendored reference implementation. About 30 lines. |
| Cyclic Redundancy Check (CRC) | A small, vendored implementation. About 12 lines. | `binascii.crc_hqx`, already in the Python standard library | A small, vendored implementation. About 12 lines. |
| Serial transport | `esp_tinyusb`, an ESP-IDF managed component | `pyserial` (from PyPI) | The Web Serial API, built into the browser. No library needed. |

## Finding the port, or ports

| Operating System | CDC0 (commands and samples) | CDC1 (logs, if enabled) |
|---|---|---|
| macOS | `/dev/cu.usbmodem*` | `/dev/cu.usbmodem*`, the second match once sorted |
| Linux | `/dev/ttyACM*` | `/dev/ttyACM*`, the second match once sorted |
| Windows | A `COM` port, shown in Device Manager | A second `COM` port, shown alongside the first |

On macOS, use the `cu` device, not the `tty` device: the `tty` device waits for a carrier
signal this device never raises.

On Linux, `ModemManager` sometimes probes a new serial device by sending it AT commands,
which this device does not understand. If a connection attempt fails right after a device is
plugged in, check `ModemManager`'s logs, or disable probing for this device's vendor and
product ID.

## Reference client, in Python

```python
import serial, cbor2, struct
from cobs import cobs

def crc16(data):
    return struct.pack(">H", __import__("binascii").crc_hqx(data, 0xFFFF))

def send(port, seq, verb, *args):
    payload = cbor2.dumps([0, seq, verb, *args])
    framed = payload + crc16(payload)
    port.write(cobs.encode(framed) + b"\x00")

def read_frame(port):
    buf = bytearray()
    while True:
        b = port.read(1)
        if not b:
            continue
        if b == b"\x00":
            framed = cobs.decode(bytes(buf))
            payload, received_crc = framed[:-2], framed[-2:]
            if crc16(payload) != received_crc:
                raise ValueError("CRC mismatch")
            return cbor2.loads(payload)
        buf += b

port = serial.Serial("/dev/cu.usbmodem1101", baudrate=115200, timeout=1)
send(port, 1, "PING")
print(read_frame(port))  # [1, 1, "PING"]
```

## Reference client, in a web browser

See `docs/17-web-control-panel.md` for the full control panel. The Web Serial API opens the
device's CDC0 port directly, with no separate host program: a person clicks a button, and the
browser asks them to choose the device.

## Web Serial, not WebUSB

The device's two ports use the standard CDC-ACM class, already claimed by the operating
system's own serial driver on every major platform. The WebUSB API can only open an interface
that no driver has already claimed, so it cannot reach this device. The Web Serial API is
built to work through the operating system's serial driver, so it is the correct choice here.
