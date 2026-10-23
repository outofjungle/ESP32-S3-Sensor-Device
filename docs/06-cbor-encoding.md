# CBOR Encoding

This document describes the three steps that turn one frame's content (`docs/05-message-model.md`)
into bytes on the wire: Concise Binary Object Representation (CBOR) encoding, a Cyclic
Redundancy Check (CRC), and Consistent Overhead Byte Stuffing (COBS). See
`docs/00-overview.md` for term definitions.

## Step 1: CBOR encoding of the payload

The frame's content, a CBOR array, is encoded to bytes using standard CBOR rules. This
project's firmware uses TinyCBOR (`components/tinycbor`); a host uses any CBOR library, or the
small hand-written subset the web control panel carries. See
`docs/17-web-control-panel.md`.

## Step 2: CRC

| Parameter | Value |
|---|---|
| Algorithm | CRC-16/CCITT-FALSE |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input reflection | None |
| Output reflection | None |
| Final XOR | None |

The CRC is computed over the CBOR-encoded payload, then appended as two bytes, high byte
first (big-endian).

### Computing the same CRC on a host computer

Python's standard library computes this exact variant: `binascii.crc_hqx(payload, 0xFFFF)`.

### Handling a CRC failure

A frame whose CRC does not match the bytes received is not answered at all. The device counts
it (see `STAT` in `docs/07-command-reference.md`) and waits for the next frame. See
`docs/11-error-codes.md` for why.

## Step 3: COBS and the frame delimiter

The CBOR payload plus its two CRC bytes are encoded with COBS, which removes every `0x00` byte
from the result. A single `0x00` byte, appended after the COBS-encoded bytes, marks the end of
the frame. A reader splits incoming bytes on `0x00` with no need to know the frame's length in
advance.

## Frame length limit

A full wire frame, COBS overhead and the trailing `0x00` included, does not exceed 256 bytes. The
largest CBOR payload that always fits is 251 bytes. With the 2-byte CRC that is 253 bytes, which COBS
encodes to 254; one more byte needs a second COBS code byte and a 257-byte frame.

## Resynchronizing after a bad frame

Because every frame ends with `0x00`, and COBS guarantees no other `0x00` byte appears before
it, a reader that loses track of where a frame starts only needs to skip to the next `0x00`
byte to recover.

## Worked example: a PING command

The command frame's content is the array `[0, 1, "PING"]`. This table shows every step, with
real, computed byte values.

| Step | Bytes (hexadecimal) | Length |
|---|---|---|
| 1. CBOR payload | `83 00 01 64 50 49 4E 47` | 8 bytes |
| 2. Framed payload (payload + CRC `3157`) | `83 00 01 64 50 49 4E 47 31 57` | 10 bytes |
| 3. COBS-encoded | `02 83 09 01 64 50 49 4E 47 31 57` | 11 bytes |
| 3. Full wire frame (with trailing `00`) | `02 83 09 01 64 50 49 4E 47 31 57 00` | 12 bytes |

Reading the CBOR payload byte by byte:

| Bytes | Meaning |
|---|---|
| `83` | An array of 3 items follows. |
| `00` | Unsigned integer `0` -- the kind, `CMD`. |
| `01` | Unsigned integer `1` -- the sequence number. |
| `64 50 49 4E 47` | A text string of length 4: `"PING"`. |

## Worked example: a sample frame

The array `[4, 12, 245000, "tof0.dist", 1523, 0]` -- sample 12, timestamp 245000 microseconds,
channel `tof0.dist`, value 1523 millimeters, no flags set.

| Step | Bytes (hexadecimal) | Length |
|---|---|---|
| 1. CBOR payload | `86 04 0C 1A 00 03 BD 08 69 74 6F 66 30 2E 64 69 73 74 19 05 F3 00` | 22 bytes |
| 2. Framed payload (payload + CRC `DFC3`) | `86 04 0C 1A 00 03 BD 08 69 74 6F 66 30 2E 64 69 73 74 19 05 F3 00 DF C3` | 24 bytes |
| 3. Full wire frame | `05 86 04 0C 1A 11 03 BD 08 69 74 6F 66 30 2E 64 69 73 74 19 05 F3 03 DF C3 00` | 25 bytes |

Reading the CBOR payload:

| Bytes | Meaning |
|---|---|
| `86` | An array of 6 items follows. |
| `04` | Unsigned integer `4` -- the kind, `DATA`. |
| `0C` | Unsigned integer `12` -- the sequence number. |
| `1A 00 03 BD 08` | Unsigned integer `245000`, in the 4-byte form (needed because the value is 65536 or greater). |
| `69 74 6F 66 30 2E 64 69 73 74` | A text string of length 9: `"tof0.dist"`. |
| `19 05 F3` | Unsigned integer `1523`, in the 2-byte form. |
| `00` | Unsigned integer `0` -- the flags. |

A timestamp above 65535 microseconds (about 65 milliseconds of uptime) always needs this
4-byte form; a decoder that assumes the 2-byte form will misread every frame after that point.
