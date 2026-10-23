// Unit tests for components/device_proto/src/proto_frame.c.
#include "tinytest.h"
#include "proto_frame.h"
#include "protocol.h"
#include "cobs.h"
#include "crc16.h"
#include <stdint.h>

TEST(ping_encodes_to_docs_06_bytes)
{
    const uint8_t ping[] = { 0x83, 0x00, 0x01, 0x64, 0x50, 0x49, 0x4E, 0x47 };
    const uint8_t want[] = { 0x02, 0x83, 0x09, 0x01, 0x64, 0x50, 0x49, 0x4E, 0x47, 0x31, 0x57, 0x00 };
    uint8_t out[32];
    size_t n;
    CHECK(proto_frame_encode(ping, sizeof(ping), out, sizeof(out), &n));
    CHECK_EQ(n, sizeof(want));
    CHECK(memcmp(out, want, sizeof(want)) == 0);
}

TEST(sample_encodes_to_docs_06_bytes)
{
    const uint8_t s[] = { 0x86, 0x04, 0x0C, 0x1A, 0x00, 0x03, 0xBD, 0x08, 0x69, 0x74, 0x6F,
                          0x66, 0x30, 0x2E, 0x64, 0x69, 0x73, 0x74, 0x19, 0x05, 0xF3, 0x00 };
    const uint8_t want[] = { 0x05, 0x86, 0x04, 0x0C, 0x1A, 0x11, 0x03, 0xBD, 0x08, 0x69, 0x74, 0x6F, 0x66,
                             0x30, 0x2E, 0x64, 0x69, 0x73, 0x74, 0x19, 0x05, 0xF3, 0x03, 0xDF, 0xC3, 0x00 };
    uint8_t out[64];
    size_t n;
    CHECK(proto_frame_encode(s, sizeof(s), out, sizeof(out), &n));
    CHECK_EQ(n, sizeof(want));
    CHECK(memcmp(out, want, sizeof(want)) == 0);
}

TEST(empty_payload_round_trips)
{
    uint8_t wire[16], back[16];
    size_t wl, bl = 99;
    CHECK(proto_frame_encode(NULL, 0, wire, sizeof(wire), &wl));
    CHECK_EQ(wire[wl - 1], 0x00);
    CHECK(proto_frame_decode(wire, wl - 1, back, sizeof(back), &bl));
    CHECK_EQ(bl, 0u);
}

TEST(encode_ends_with_single_delimiter)
{
    uint8_t p[40], out[80];
    for (int i = 0; i < 40; i++)
        p[i] = (uint8_t)(i % 3);
    size_t n;
    CHECK(proto_frame_encode(p, sizeof(p), out, sizeof(out), &n));
    CHECK_EQ(out[n - 1], 0x00);
    for (size_t i = 0; i + 1 < n; i++)
        CHECK(out[i] != 0x00);
}

TEST(encode_payload_size_limit)
{
    uint8_t p[400] = { 0 };
    uint8_t out[700];
    size_t n;
    CHECK(proto_frame_encode(p, PROTO_DECODE_CAP - 2, out, sizeof(out), &n));
    CHECK(!proto_frame_encode(p, PROTO_DECODE_CAP - 1, out, sizeof(out), &n));
}

TEST(encode_small_output_capacity_fails)
{
    const uint8_t p[] = { 1, 2, 3 };
    uint8_t out[16];
    size_t n;
    CHECK(!proto_frame_encode(p, 3, out, 0, &n));
    CHECK(!proto_frame_encode(p, 3, out, 1, &n));
    CHECK(proto_frame_encode(p, 3, out, sizeof(out), &n));
    CHECK(!proto_frame_encode(p, 3, out, n - 1, &n));
}

TEST(max_payload_fits_in_max_wire_frame)
{
    // The worst case is a payload with no zero bytes: COBS then adds an
    // extra code byte at each 254-byte boundary. docs/06-cbor-encoding.md
    // promises the whole wire frame, delimiter included, is <= 256 bytes.
    uint8_t p[PROTO_MAX_PAYLOAD];
    uint8_t out[PROTO_DECODE_CAP + 8];
    memset(p, 0x55, sizeof(p));
    size_t n;
    CHECK(proto_frame_encode(p, sizeof(p), out, sizeof(out), &n));
    CHECK(n <= PROTO_MAX_WIRE_FRAME);
}

TEST(round_trip_many_lengths)
{
    uint8_t p[300], wire[PROTO_DECODE_CAP + 8], back[PROTO_DECODE_CAP];
    for (size_t len = 0; len <= 300; len++) {
        for (size_t i = 0; i < len; i++)
            p[i] = (uint8_t)((i + len) % 9);
        size_t wl, bl = 0;
        CHECK(proto_frame_encode(p, len, wire, sizeof(wire), &wl));
        CHECK(proto_frame_decode(wire, wl - 1, back, sizeof(back), &bl));
        CHECK_EQ(bl, len);
        CHECK(memcmp(p, back, len) == 0);
    }
}

TEST(decode_rejects_any_single_bit_flip)
{
    const uint8_t p[] = { 0x83, 0x00, 0x01, 0x64, 0x50, 0x49, 0x4E, 0x47 };
    uint8_t wire[32], back[32];
    size_t wl, bl;
    CHECK(proto_frame_encode(p, sizeof(p), wire, sizeof(wire), &wl));
    size_t body = wl - 1;
    for (size_t i = 0; i < body; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t save = wire[i];
            wire[i] ^= (uint8_t)(1u << bit);
            // Either COBS rejects it, or the CRC does. Never accepted as
            // the original payload.
            bool ok = proto_frame_decode(wire, body, back, sizeof(back), &bl);
            CHECK(!ok || bl != sizeof(p) || memcmp(back, p, sizeof(p)) != 0);
            CHECK(!ok);
            wire[i] = save;
        }
    }
}

TEST(decode_rejects_swapped_crc_bytes)
{
    const uint8_t p[] = { 0x01 };
    uint16_t crc = crc16_ccitt_false(p, 1);
    CHECK((crc >> 8) != (crc & 0xFF));
    uint8_t good[3] = { 0x01, (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF) };
    uint8_t swapped[3] = { 0x01, (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8) };
    uint8_t wire[16], back[16];
    size_t wl, bl;
    CHECK(cobs_encode(good, 3, wire, sizeof(wire), &wl));
    CHECK(proto_frame_decode(wire, wl, back, sizeof(back), &bl));
    CHECK(cobs_encode(swapped, 3, wire, sizeof(wire), &wl));
    CHECK(!proto_frame_decode(wire, wl, back, sizeof(back), &bl));
}

TEST(decode_rejects_too_short)
{
    uint8_t back[8];
    size_t bl;
    CHECK(!proto_frame_decode(NULL, 0, back, sizeof(back), &bl));
    const uint8_t one[] = { 0x02, 0x11 }; // decodes to 1 byte
    CHECK(!proto_frame_decode(one, sizeof(one), back, sizeof(back), &bl));
}

TEST(decode_output_capacity)
{
    const uint8_t p[] = { 1, 2, 3, 4, 5 };
    uint8_t wire[32], back[8];
    size_t wl, bl;
    CHECK(proto_frame_encode(p, 5, wire, sizeof(wire), &wl));
    CHECK(!proto_frame_decode(wire, wl - 1, back, 4, &bl));
    CHECK(proto_frame_decode(wire, wl - 1, back, 5, &bl));
    CHECK_EQ(bl, 5u);
}

TEST(decode_rejects_oversize_frame)
{
    // Two full 0xFF COBS blocks decode to 508 bytes, over PROTO_DECODE_CAP.
    uint8_t wire[600], back[600];
    size_t pos = 0, bl;
    for (int blk = 0; blk < 2; blk++) {
        wire[pos++] = 0xFF;
        memset(&wire[pos], 0x11, 254);
        pos += 254;
    }
    wire[pos++] = 0x01;
    CHECK(!proto_frame_decode(wire, pos, back, sizeof(back), &bl));
}

int main(void)
{
    RUN_TEST(ping_encodes_to_docs_06_bytes);
    RUN_TEST(sample_encodes_to_docs_06_bytes);
    RUN_TEST(empty_payload_round_trips);
    RUN_TEST(encode_ends_with_single_delimiter);
    RUN_TEST(encode_payload_size_limit);
    RUN_TEST(encode_small_output_capacity_fails);
    RUN_TEST(max_payload_fits_in_max_wire_frame);
    RUN_TEST(round_trip_many_lengths);
    RUN_TEST(decode_rejects_any_single_bit_flip);
    RUN_TEST(decode_rejects_swapped_crc_bytes);
    RUN_TEST(decode_rejects_too_short);
    RUN_TEST(decode_output_capacity);
    RUN_TEST(decode_rejects_oversize_frame);
    return tinytest_summary();
}
