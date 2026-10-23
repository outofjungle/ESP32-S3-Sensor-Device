#pragma once

#include <stddef.h>
#include <stdint.h>
#include "cbor.h"

// Builds and sends response frames: OK, ERR, and ROW. See
// docs/05-message-model.md. Any verb handler uses these, whether it is one
// of device_proto's own built-in verbs or one registered by sensor_core
// (see proto_cmd_register_verb() in proto_cmd.h).
//
// All three "send" functions use one shared static transmit buffer, sized
// PROTO_MAX_PAYLOAD. This is safe only because every command, from every
// verb, runs to completion on the single usb_rx task before the next frame
// is read -- see docs/12-flow-control-and-backpressure.md.

// Starts a response array of total_items items: kind, seq, verb, then
// (total_items - 3) more values the caller encodes into *arr before calling
// proto_reply_send().
void proto_reply_begin(CborEncoder *enc, CborEncoder *arr, int kind, uint32_t seq, const char *verb,
                       size_t total_items);

// Closes the array started by proto_reply_begin() and sends it as one
// response frame.
void proto_reply_send(CborEncoder *enc, CborEncoder *arr);

// Sends an OK frame with no extra values: [1, seq, verb].
void proto_reply_ok0(uint32_t seq, const char *verb);

// Sends an OK frame with one unsigned integer value: [1, seq, verb, value].
void proto_reply_ok_uint(uint32_t seq, const char *verb, uint64_t value);

// Sends an OK frame with one signed integer value: [1, seq, verb, value].
void proto_reply_ok_int(uint32_t seq, const char *verb, int64_t value);

// Sends an OK frame with one boolean value: [1, seq, verb, value].
void proto_reply_ok_bool(uint32_t seq, const char *verb, bool value);

// Sends an OK frame with one text value: [1, seq, verb, value].
void proto_reply_ok_text(uint32_t seq, const char *verb, const char *value);

// Sends an ERR frame: [2, seq, verb, code]. code is one of the text codes
// in docs/11-error-codes.md, e.g. "ERANGE".
void proto_reply_err(uint32_t seq, const char *verb, const char *code);
