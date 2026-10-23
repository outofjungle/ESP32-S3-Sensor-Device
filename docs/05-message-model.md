# Message Model

This document describes the shape of a frame's content. `docs/06-cbor-encoding.md` describes
how that content becomes bytes on the wire. See `docs/00-overview.md` for term definitions.

## Frame content is one CBOR array

Every frame's content is one Concise Binary Object Representation (CBOR) array. The array's
first item is always the frame kind: an integer naming what the rest of the array holds.

## Frame kinds

| Kind number | Name | Sent by | Purpose |
|---|---|---|---|
| 0 | `CMD` | Host | Asks the device to run one command. |
| 1 | `OK` | Device | A command succeeded. |
| 2 | `ERR` | Device | A command failed. |
| 3 | `ROW` | Device | One row of a multi-row answer. One or more `ROW` frames are followed by exactly one `OK` frame, carrying the row count. |
| 4 | `DATA` | Device | One sample, sent while streaming, with no matching command. |

## Command frame

```
[0, seq, verb, arg, arg, ...]
```

| Position | Item | Type | Meaning |
|---|---|---|---|
| 0 | Kind | Integer | Always 0. |
| 1 | Sequence number | Integer | 0 to 65535. The response copies this value back. |
| 2 | Verb | Text | The command name, e.g. `"GET"`. See `docs/07-command-reference.md`. |
| 3+ | Arguments | Any | Zero or more, depending on the verb. |

## Response frame

```
[1, seq, verb, value, ...]      // OK
[2, seq, verb, errcode]          // ERR
```

An `ERR` frame always carries exactly one value: the error code text, e.g. `"ERANGE"`. See
`docs/11-error-codes.md`.

## Multi-row answers

A command that answers with more than one row (`CAPS`, `GET *`, `SENS`, `SCAPS`) sends one
`ROW` frame per row, then one `OK` frame carrying the row count:

```
[3, seq, verb, col, col, ...]   // repeated, once per row
[3, seq, verb, col, col, ...]
[1, seq, verb, row_count]        // once, at the end
```

## Sample frame

```
[4, seq, t_us, channel, value, flags]
```

| Position | Item | Type | Meaning |
|---|---|---|---|
| 0 | Kind | Integer | Always 4. |
| 1 | Sequence number | Integer | The device's own counter for `DATA` frames, separate from the host's command sequence numbers. Wraps at 65535. |
| 2 | Timestamp | Integer | Microseconds since the device started. |
| 3 | Channel | Text | The channel's full name, e.g. `"tof0.dist"`. |
| 4 | Value | Any | The channel's current value. May be CBOR `null`; see `docs/08-channel-model-and-types.md`. |
| 5 | Flags | Integer | A bit field. See `docs/08-channel-model-and-types.md`. |

## Why a value needs no separate type field

CBOR itself carries each value's type (an integer, text, a boolean, `null`, or a byte string).
A frame never needs to say "this value is a number" separately from the value.

## Sequence numbers

A command's sequence number is chosen by the host, 0 to 65535, and copied unchanged into every
response to that command, including each `ROW` frame. A `DATA` frame's sequence number is
chosen by the device, counting up from 0 and wrapping at 65535, independent of command sequence
numbers.

## Where the frame's content ends and the wire bytes begin

This document describes the CBOR array only: the frame's content. `docs/06-cbor-encoding.md`
describes the three more steps that turn that array into the bytes actually sent: CBOR
encoding, a Cyclic Redundancy Check (CRC), and Consistent Overhead Byte Stuffing (COBS).
