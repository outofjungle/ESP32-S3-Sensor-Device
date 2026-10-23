// Unit tests for components/device_proto/src/cobs.c.
// Build and run: see README.md.

#include "tinytest.h"
#include "cobs.h"
#include <stdint.h>
#include <stdlib.h>

static bool enc(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *olen)
{
    return cobs_encode(in, n, out, cap, olen);
}

TEST(encode_empty_is_single_code_byte)
{
    uint8_t out[4];
    size_t n = 99;
    CHECK(enc(NULL, 0, out, sizeof(out), &n));
    CHECK_EQ(n, 1u);
    CHECK_EQ(out[0], 0x01);
}

TEST(encode_known_vectors)
{
    uint8_t out[16];
    size_t n;
    const uint8_t a[] = { 0x00 };
    CHECK(enc(a, 1, out, sizeof(out), &n));
    CHECK_EQ(n, 2u);
    CHECK(out[0] == 0x01 && out[1] == 0x01);

    const uint8_t b[] = { 0x00, 0x00 };
    CHECK(enc(b, 2, out, sizeof(out), &n));
    CHECK_EQ(n, 3u);
    CHECK(out[0] == 0x01 && out[1] == 0x01 && out[2] == 0x01);

    const uint8_t c[] = { 0x11, 0x22, 0x00, 0x33 };
    CHECK(enc(c, 4, out, sizeof(out), &n));
    const uint8_t want[] = { 0x03, 0x11, 0x22, 0x02, 0x33 };
    CHECK_EQ(n, sizeof(want));
    CHECK(memcmp(out, want, sizeof(want)) == 0);
}

TEST(encode_zero_capacity_fails)
{
    uint8_t out[1];
    size_t n = 0;
    CHECK(!enc(NULL, 0, out, 0, &n));
}

TEST(encode_runs_out_of_room_while_starting_a_new_block)
{
    uint8_t out[300];
    size_t n;
    const uint8_t z[] = { 0x00 };
    CHECK(!enc(z, 1, out, 1, &n)); // one code byte fits, the next block's does not
    CHECK(enc(z, 1, out, 2, &n));
    uint8_t nz[300];
    memset(nz, 0x55, sizeof(nz));
    CHECK(!enc(nz, 254, out, 255, &n)); // the 0xFF block fills the buffer; its successor needs a byte
}

TEST(encode_exact_capacity_ok_one_short_fails)
{
    const uint8_t in[] = { 0x11, 0x22, 0x00, 0x33 };
    uint8_t out[5];
    size_t n;
    CHECK(enc(in, 4, out, 5, &n));
    CHECK_EQ(n, 5u);
    CHECK(!enc(in, 4, out, 4, &n));
}

TEST(encode_never_writes_past_capacity)
{
    uint8_t in[300];
    for (size_t i = 0; i < sizeof(in); i++)
        in[i] = (uint8_t)(i % 7 == 0 ? 0 : i);
    for (size_t cap = 1; cap < 40; cap++) {
        uint8_t guard[64];
        memset(guard, 0xAA, sizeof(guard));
        size_t n;
        (void)enc(in, 100, guard, cap, &n);
        for (size_t i = cap; i < sizeof(guard); i++) {
            CHECK_EQ(guard[i], 0xAA);
        }
    }
}

TEST(encode_no_zero_bytes_in_output)
{
    uint8_t in[600];
    uint8_t out[700];
    for (size_t len = 0; len <= 600; len += 7) {
        for (size_t i = 0; i < len; i++)
            in[i] = (uint8_t)((i * 31) % 4);
        size_t n;
        CHECK(enc(in, len, out, sizeof(out), &n));
        for (size_t i = 0; i < n; i++)
            CHECK(out[i] != 0);
    }
}

TEST(encode_long_nonzero_runs)
{
    uint8_t in[260];
    uint8_t out[300];
    memset(in, 0x55, sizeof(in));
    size_t n;
    CHECK(enc(in, 253, out, sizeof(out), &n));
    CHECK_EQ(n, 254u);
    CHECK(enc(in, 254, out, sizeof(out), &n));
    CHECK_EQ(n, 256u); // 0xFF block, then a trailing 0x01 code byte
    CHECK_EQ(out[0], 0xFF);
    CHECK_EQ(out[255], 0x01);
    CHECK(enc(in, 255, out, sizeof(out), &n));
    CHECK_EQ(n, 257u);
}

TEST(decode_empty_input_gives_empty_output)
{
    uint8_t out[4];
    size_t n = 99;
    CHECK(cobs_decode(NULL, 0, out, sizeof(out), &n) || n == 99);
}

TEST(decode_rejects_embedded_zero_code)
{
    const uint8_t bad[] = { 0x03, 0x11, 0x00 };
    uint8_t out[8];
    size_t n;
    CHECK(!cobs_decode(bad, sizeof(bad), out, sizeof(out), &n));
    const uint8_t bad2[] = { 0x00 };
    CHECK(!cobs_decode(bad2, 1, out, sizeof(out), &n));
}

TEST(decode_rejects_code_pointing_past_end)
{
    const uint8_t bad[] = { 0x05, 0x01, 0x02 };
    uint8_t out[8];
    size_t n;
    CHECK(!cobs_decode(bad, sizeof(bad), out, sizeof(out), &n));
}

TEST(decode_rejects_output_overflow)
{
    const uint8_t in[] = { 0x11, 0x22, 0x00, 0x33 };
    uint8_t enc_buf[8];
    size_t elen;
    CHECK(enc(in, 4, enc_buf, sizeof(enc_buf), &elen));
    uint8_t out[8];
    size_t n;
    for (size_t cap = 0; cap < 4; cap++) {
        CHECK(!cobs_decode(enc_buf, elen, out, cap, &n));
    }
    CHECK(cobs_decode(enc_buf, elen, out, 4, &n));
    CHECK_EQ(n, 4u);
}

TEST(decode_known_noncanonical_inputs)
{
    uint8_t out[4];
    size_t n;
    const uint8_t a[] = { 0x01 };
    CHECK(cobs_decode(a, 1, out, sizeof(out), &n));
    CHECK_EQ(n, 0u);
    const uint8_t b[] = { 0x01, 0x01 };
    CHECK(cobs_decode(b, 2, out, sizeof(out), &n));
    CHECK_EQ(n, 1u);
    CHECK_EQ(out[0], 0x00);
}

TEST(round_trip_all_lengths_0_to_600)
{
    uint8_t in[600];
    uint8_t mid[700];
    uint8_t back[700];
    for (size_t len = 0; len <= 600; len++) {
        for (size_t i = 0; i < len; i++) {
            in[i] = (len % 3 == 0) ? 0x42 : (uint8_t)((i * 13 + len) % 5);
        }
        size_t el, dl = 0;
        CHECK(enc(in, len, mid, sizeof(mid), &el));
        CHECK(cobs_decode(mid, el, back, sizeof(back), &dl));
        CHECK_EQ(dl, len);
        CHECK(memcmp(in, back, len) == 0);
    }
}

int main(void)
{
    RUN_TEST(encode_empty_is_single_code_byte);
    RUN_TEST(encode_known_vectors);
    RUN_TEST(encode_zero_capacity_fails);
    RUN_TEST(encode_runs_out_of_room_while_starting_a_new_block);
    RUN_TEST(encode_exact_capacity_ok_one_short_fails);
    RUN_TEST(encode_never_writes_past_capacity);
    RUN_TEST(encode_no_zero_bytes_in_output);
    RUN_TEST(encode_long_nonzero_runs);
    RUN_TEST(decode_empty_input_gives_empty_output);
    RUN_TEST(decode_rejects_embedded_zero_code);
    RUN_TEST(decode_rejects_code_pointing_past_end);
    RUN_TEST(decode_rejects_output_overflow);
    RUN_TEST(decode_known_noncanonical_inputs);
    RUN_TEST(round_trip_all_lengths_0_to_600);
    return tinytest_summary();
}
