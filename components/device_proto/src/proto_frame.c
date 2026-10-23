#include "proto_frame.h"
#include "protocol.h"
#include "crc16.h"
#include "cobs.h"
#include <string.h>

bool proto_frame_encode(const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    uint8_t framed[PROTO_DECODE_CAP];
    if (payload_len + 2 > sizeof(framed)) {
        return false;
    }
    memcpy(framed, payload, payload_len);
    uint16_t crc = crc16_ccitt_false(payload, payload_len);
    framed[payload_len] = (uint8_t)(crc >> 8);
    framed[payload_len + 1] = (uint8_t)(crc & 0xFF);
    size_t framed_len = payload_len + 2;

    if (out_cap < 1) {
        return false;
    }
    size_t encoded_len = 0;
    if (!cobs_encode(framed, framed_len, out, out_cap - 1, &encoded_len)) {
        return false;
    }
    out[encoded_len] = 0x00;
    *out_len = encoded_len + 1;
    return true;
}

bool proto_frame_decode(const uint8_t *wire, size_t wire_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    uint8_t framed[PROTO_DECODE_CAP];
    size_t framed_len = 0;
    if (!cobs_decode(wire, wire_len, framed, sizeof(framed), &framed_len)) {
        return false;
    }
    if (framed_len < 2) {
        return false;
    }
    size_t payload_len = framed_len - 2;
    uint16_t received_crc = ((uint16_t)framed[payload_len] << 8) | framed[payload_len + 1];
    uint16_t computed_crc = crc16_ccitt_false(framed, payload_len);
    if (received_crc != computed_crc) {
        return false;
    }
    if (payload_len > out_cap) {
        return false;
    }
    memcpy(out, framed, payload_len);
    *out_len = payload_len;
    return true;
}
