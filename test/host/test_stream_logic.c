// Unit tests for components/device_proto/src/stream_logic.c.
#include "tinytest.h"
#include "stream_logic.h"
#include <stdint.h>

TEST(sequence_wraps_at_65535)
{
    uint32_t s = 65534;
    CHECK_EQ(stream_seq_next(&s), 65534u);
    CHECK_EQ(stream_seq_next(&s), 65535u);
    CHECK_EQ(stream_seq_next(&s), 0u);
    CHECK_EQ(stream_seq_next(&s), 1u);
}

TEST(sequence_starts_at_zero_and_counts_up)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < 10; i++)
        CHECK_EQ(stream_seq_next(&s), i);
}

TEST(a_full_cycle_returns_to_zero)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < 65536; i++)
        stream_seq_next(&s);
    CHECK_EQ(s, 0u);
}

TEST(sequence_above_the_maximum_restarts_at_zero)
{
    uint32_t s = 70000;
    CHECK_EQ(stream_seq_next(&s), 0u);
    CHECK_EQ(s, 1u);
    s = UINT32_MAX;
    CHECK_EQ(stream_seq_next(&s), 0u);
}

static esp_err_t get_1523(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    cbor_encode_uint(enc, 1523);
    return ESP_OK;
}
static esp_err_t get_nothing(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    (void)enc;
    return ESP_FAIL;
}
static esp_err_t get_big(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    static uint8_t blob[300];
    cbor_encode_byte_string(enc, blob, sizeof(blob));
    return ESP_OK;
}

static channel_entry_t make(const char *name, channel_get_fn g)
{
    channel_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.get = g;
    return e;
}

TEST(sample_matches_the_docs_06_worked_example)
{
    // docs/06-cbor-encoding.md: [4, 12, 245000, "tof0.dist", 1523, 0]
    const uint8_t want[] = { 0x86, 0x04, 0x0C, 0x1A, 0x00, 0x03, 0xBD, 0x08, 0x69, 0x74, 0x6F,
                             0x66, 0x30, 0x2E, 0x64, 0x69, 0x73, 0x74, 0x19, 0x05, 0xF3, 0x00 };
    channel_entry_t ch = make("tof0.dist", get_1523);
    uint8_t buf[64];
    size_t len = 0;
    CHECK(stream_build_sample(buf, sizeof(buf), 12, 245000, &ch, 0, &len));
    CHECK_EQ(len, sizeof(want));
    CHECK(memcmp(buf, want, sizeof(want)) == 0);
}

TEST(flags_are_carried)
{
    channel_entry_t ch = make("x", get_1523);
    uint8_t buf[64];
    size_t len = 0;
    CHECK(stream_build_sample(buf, sizeof(buf), 1, 1, &ch, 5, &len));
    CHECK_EQ(buf[len - 1], 5);
}

TEST(an_incomplete_or_oversize_sample_is_not_built)
{
    channel_entry_t bad = make("x", get_nothing);
    channel_entry_t big = make("x", get_big);
    uint8_t buf[64];
    size_t len = 77;
    CHECK(!stream_build_sample(buf, sizeof(buf), 1, 1, &bad, 0, &len));
    CHECK(!stream_build_sample(buf, sizeof(buf), 1, 1, &big, 0, &len));
    CHECK_EQ(len, 77u); // untouched on failure
    uint8_t small[8];
    channel_entry_t ok = make("a_long_channel_name", get_1523);
    CHECK(!stream_build_sample(small, sizeof(small), 1, 1, &ok, 0, &len));
}

int main(void)
{
    RUN_TEST(sequence_wraps_at_65535);
    RUN_TEST(sequence_starts_at_zero_and_counts_up);
    RUN_TEST(a_full_cycle_returns_to_zero);
    RUN_TEST(sequence_above_the_maximum_restarts_at_zero);
    RUN_TEST(sample_matches_the_docs_06_worked_example);
    RUN_TEST(flags_are_carried);
    RUN_TEST(an_incomplete_or_oversize_sample_is_not_built);
    return tinytest_summary();
}
