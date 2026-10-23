#pragma once

// Shared constants for the frame protocol. See docs/05-message-model.md and
// docs/06-cbor-encoding.md.

// Frame kinds. The first item of every frame's CBOR array.
#define FRAME_KIND_CMD  0
#define FRAME_KIND_OK   1
#define FRAME_KIND_ERR  2
#define FRAME_KIND_ROW  3
#define FRAME_KIND_DATA 4

// A full wire frame, including COBS overhead and the trailing 0x00
// delimiter, does not exceed this many bytes. See docs/06-cbor-encoding.md.
#define PROTO_MAX_WIRE_FRAME 256

// The largest CBOR payload that always fits inside PROTO_MAX_WIRE_FRAME
// after the CRC (2 bytes), COBS overhead, and the delimiter (1 byte) are
// added. COBS adds 1 byte for up to 253 bytes of input, but 2 bytes at 254
// (a full 0xFF block plus a trailing code byte). So the CRC-extended input
// must stay at 253 bytes or fewer: 256 - 1 (delimiter) - 1 (COBS) - 2 (CRC)
// - 1 (the 254-byte block boundary). test/host/test_proto_frame.c checks
// this with a payload that has no zero bytes, the worst case.
#define PROTO_MAX_PAYLOAD (PROTO_MAX_WIRE_FRAME - 2 - 1 - 1 - 1)

// A little more than PROTO_MAX_WIRE_FRAME, so a moderately oversized but
// otherwise well-formed incoming frame can still be decoded far enough to
// recover a trustworthy sequence number and answer with an error (see
// docs/11-error-codes.md), instead of being silently dropped as unreadable.
// Used both for the receive accumulator in usb_cdc.c and for
// proto_frame_decode()'s internal scratch buffer in proto_frame.c -- both
// must agree, or a frame this large will decode in one place and fail in
// the other.
#define PROTO_DECODE_CAP (PROTO_MAX_WIRE_FRAME + 64)

// Flags bits carried in a sample frame. See docs/08-channel-model-and-types.md.
#define FLAG_DROP  (1u << 0)
#define FLAG_STALE (1u << 1)
#define FLAG_SAT   (1u << 2)

// A channel or config key name, including any instance prefix ("tof0.dist"),
// including the terminating NUL. See docs/08-channel-model-and-types.md.
#define PROTO_NAME_MAXLEN 40
