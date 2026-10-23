#include "cobs.h"
#include <string.h>

bool cobs_encode(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (out_cap < 1) {
        return false;
    }

    size_t in_pos = 0;
    size_t out_pos = 1;  // slot 0 holds the first code byte
    size_t code_pos = 0; // index of the code byte for the current run
    uint8_t code = 1;

    while (in_pos < len) {
        if (data[in_pos] == 0) {
            out[code_pos] = code;
            code_pos = out_pos;
            if (out_pos >= out_cap) {
                return false;
            }
            out_pos++;
            code = 1;
            in_pos++;
        } else {
            if (out_pos >= out_cap) {
                return false;
            }
            out[out_pos++] = data[in_pos++];
            code++;
            if (code == 0xFF) {
                out[code_pos] = code;
                code_pos = out_pos;
                if (out_pos >= out_cap) {
                    return false;
                }
                out_pos++;
                code = 1;
            }
        }
    }
    out[code_pos] = code;

    *out_len = out_pos;
    return true;
}

bool cobs_decode(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t idx = 0;
    size_t out_pos = 0;

    while (idx < len) {
        uint8_t code = data[idx];
        if (code == 0) {
            return false;
        }
        size_t chunk_len = (size_t)code - 1;
        if (idx + 1 + chunk_len > len) {
            return false;
        }
        if (out_pos + chunk_len > out_cap) {
            return false;
        }
        // A 0x00 byte inside a block is never valid COBS: the frame
        // delimiter is the only 0x00 on the wire.
        for (size_t i = 0; i < chunk_len; i++) {
            if (data[idx + 1 + i] == 0) {
                return false;
            }
        }
        memcpy(&out[out_pos], &data[idx + 1], chunk_len);
        out_pos += chunk_len;
        idx += code;
        if (code < 0xFF && idx < len) {
            if (out_pos >= out_cap) {
                return false;
            }
            out[out_pos++] = 0;
        }
    }

    *out_len = out_pos;
    return true;
}
