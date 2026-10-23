#include "proto_reply.h"
#include "protocol.h"
#include "usb_cdc.h"
#include <string.h>

static uint8_t s_txbuf[PROTO_MAX_PAYLOAD];

// Remembered by proto_reply_begin() so proto_reply_send() can answer with an
// error for the same command if the reply does not fit in s_txbuf.
static uint32_t s_seq;
static char s_verb[16];

void proto_reply_begin(CborEncoder *enc, CborEncoder *arr, int kind, uint32_t seq, const char *verb, size_t total_items)
{
    s_seq = seq;
    strncpy(s_verb, verb, sizeof(s_verb) - 1);
    s_verb[sizeof(s_verb) - 1] = '\0';
    cbor_encoder_init(enc, s_txbuf, sizeof(s_txbuf), 0);
    cbor_encoder_create_array(enc, arr, total_items);
    cbor_encode_int(arr, kind);
    cbor_encode_uint(arr, seq);
    cbor_encode_text_stringz(arr, verb);
}

void proto_reply_send(CborEncoder *enc, CborEncoder *arr)
{
    // tinycbor reports a value that did not fit only here, at the close, and
    // its buffer-size call is meaningless after that. Never send what the
    // encoder could not finish; answer with a short error instead, which
    // always fits. See AGENTS.md, "Firmware mistakes to avoid".
    if (cbor_encoder_close_container(enc, arr) != CborNoError) {
        CborEncoder e2, a2;
        cbor_encoder_init(&e2, s_txbuf, sizeof(s_txbuf), 0);
        cbor_encoder_create_array(&e2, &a2, 4);
        cbor_encode_int(&a2, FRAME_KIND_ERR);
        cbor_encode_uint(&a2, s_seq);
        cbor_encode_text_stringz(&a2, s_verb);
        cbor_encode_text_stringz(&a2, "EINTERNAL");
        cbor_encoder_close_container(&e2, &a2);
        usb_cdc_send_response_frame(s_txbuf, cbor_encoder_get_buffer_size(&e2, s_txbuf));
        return;
    }
    size_t len = cbor_encoder_get_buffer_size(enc, s_txbuf);
    usb_cdc_send_response_frame(s_txbuf, len);
}

void proto_reply_ok0(uint32_t seq, const char *verb)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, verb, 3);
    proto_reply_send(&enc, &arr);
}

void proto_reply_ok_uint(uint32_t seq, const char *verb, uint64_t value)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, verb, 4);
    cbor_encode_uint(&arr, value);
    proto_reply_send(&enc, &arr);
}

void proto_reply_ok_int(uint32_t seq, const char *verb, int64_t value)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, verb, 4);
    cbor_encode_int(&arr, value);
    proto_reply_send(&enc, &arr);
}

void proto_reply_ok_bool(uint32_t seq, const char *verb, bool value)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, verb, 4);
    cbor_encode_boolean(&arr, value);
    proto_reply_send(&enc, &arr);
}

void proto_reply_ok_text(uint32_t seq, const char *verb, const char *value)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, verb, 4);
    cbor_encode_text_stringz(&arr, value);
    proto_reply_send(&enc, &arr);
}

void proto_reply_err(uint32_t seq, const char *verb, const char *code)
{
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_ERR, seq, verb, 4);
    cbor_encode_text_stringz(&arr, code);
    proto_reply_send(&enc, &arr);
}
