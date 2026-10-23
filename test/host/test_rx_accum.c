// Unit tests for components/device_proto/src/rx_accum.c, the receiver that
// splits host bytes into frames at the 0x00 delimiter.
#include "tinytest.h"
#include "rx_accum.h"
#include <stdint.h>

static int g_frames, g_oversize;
static uint8_t g_last[PROTO_DECODE_CAP];
static size_t g_last_len;

static void on_frame(void *ctx, const uint8_t *w, size_t n)
{
    (void)ctx;
    g_frames++;
    g_last_len = n;
    memcpy(g_last, w, n);
}
static void on_oversize(void *ctx)
{
    (void)ctx;
    g_oversize++;
}
static const rx_accum_cb_t kCb = { on_frame, on_oversize };

static void fresh(rx_accum_t *a)
{
    g_frames = g_oversize = 0;
    g_last_len = 0;
    rx_accum_init(a);
}
static void feed(rx_accum_t *a, const uint8_t *b, size_t n)
{
    rx_accum_feed(a, b, n, &kCb, NULL);
}

TEST(one_frame)
{
    rx_accum_t a;
    fresh(&a);
    const uint8_t f[] = { 0x02, 0x83, 0x09, 0x00 };
    feed(&a, f, sizeof(f));
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_last_len, 3u);
    CHECK(memcmp(g_last, f, 3) == 0);
    CHECK_EQ(a.len, 0u);
}

TEST(stray_delimiters_are_ignored)
{
    rx_accum_t a;
    fresh(&a);
    uint8_t zeros[100] = { 0 };
    feed(&a, zeros, sizeof(zeros));
    CHECK_EQ(g_frames, 0);
    CHECK_EQ(g_oversize, 0);
    CHECK_EQ(a.len, 0u);
}

TEST(a_single_byte_frame_is_still_a_frame)
{
    // The decoder rejects it later; the receiver's job is only to split.
    rx_accum_t a;
    fresh(&a);
    const uint8_t f[] = { 0x05, 0x00 };
    feed(&a, f, sizeof(f));
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_last_len, 1u);
}

TEST(frame_split_across_chunks)
{
    rx_accum_t a;
    fresh(&a);
    const uint8_t p1[] = { 0x02, 0x83 };
    const uint8_t p2[] = { 0x09 };
    const uint8_t p3[] = { 0x00 };
    feed(&a, p1, sizeof(p1));
    CHECK_EQ(g_frames, 0);
    feed(&a, p2, sizeof(p2));
    CHECK_EQ(g_frames, 0);
    feed(&a, p3, sizeof(p3));
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_last_len, 3u);
}

TEST(several_frames_in_one_chunk)
{
    rx_accum_t a;
    fresh(&a);
    const uint8_t f[] = { 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x04, 0x00 };
    feed(&a, f, sizeof(f));
    CHECK_EQ(g_frames, 3);
    CHECK_EQ(g_last_len, 1u);
    CHECK_EQ(g_last[0], 0x04);
}

TEST(a_byte_at_a_time)
{
    rx_accum_t a;
    fresh(&a);
    const uint8_t f[] = { 0x03, 0x11, 0x22, 0x00, 0x02, 0x33, 0x00 };
    for (size_t i = 0; i < sizeof(f); i++)
        feed(&a, &f[i], 1);
    CHECK_EQ(g_frames, 2);
}

TEST(longest_accepted_frame)
{
    rx_accum_t a;
    fresh(&a);
    uint8_t big[PROTO_DECODE_CAP + 1];
    memset(big, 0x11, PROTO_DECODE_CAP);
    big[PROTO_DECODE_CAP] = 0x00;
    feed(&a, big, sizeof(big)); // exactly the buffer size, then the delimiter
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_oversize, 0);
    CHECK_EQ(g_last_len, (size_t)PROTO_DECODE_CAP);
}

TEST(one_byte_too_long_is_oversize_and_not_decoded)
{
    rx_accum_t a;
    fresh(&a);
    uint8_t big[PROTO_DECODE_CAP + 2];
    memset(big, 0x11, PROTO_DECODE_CAP + 1);
    big[PROTO_DECODE_CAP + 1] = 0x00;
    feed(&a, big, sizeof(big));
    CHECK_EQ(g_frames, 0); // the truncated prefix is never handed on
    CHECK_EQ(g_oversize, 1);
    CHECK_EQ(a.len, 0u);
    CHECK(!a.overflow);
}

TEST(receiver_recovers_after_an_oversize_frame)
{
    rx_accum_t a;
    fresh(&a);
    uint8_t flood[2000];
    memset(flood, 0x22, sizeof(flood));
    feed(&a, flood, sizeof(flood)); // no delimiter yet
    CHECK(a.overflow);
    const uint8_t tail[] = { 0x00, 0x02, 0x83, 0x00 }; // end the long frame, then a good one
    feed(&a, tail, sizeof(tail));
    CHECK_EQ(g_oversize, 1);
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_last_len, 2u);
}

TEST(overflow_stays_bounded_for_endless_input)
{
    rx_accum_t a;
    fresh(&a);
    uint8_t flood[1000];
    memset(flood, 0x7F, sizeof(flood));
    for (int i = 0; i < 1000; i++)
        feed(&a, flood, sizeof(flood)); // 1 MB of non-delimiter bytes
    CHECK(a.len <= sizeof(a.buf));
    CHECK_EQ(g_frames, 0);
}

TEST(null_callbacks_do_not_crash)
{
    rx_accum_t a;
    fresh(&a);
    rx_accum_cb_t none = { NULL, NULL };
    const uint8_t f[] = { 0x01, 0x00 };
    rx_accum_feed(&a, f, sizeof(f), &none, NULL);
    uint8_t big[PROTO_DECODE_CAP + 5];
    memset(big, 1, sizeof(big));
    big[sizeof(big) - 1] = 0;
    rx_accum_feed(&a, big, sizeof(big), &none, NULL);
    CHECK_EQ(a.len, 0u);
}

int main(void)
{
    RUN_TEST(one_frame);
    RUN_TEST(stray_delimiters_are_ignored);
    RUN_TEST(a_single_byte_frame_is_still_a_frame);
    RUN_TEST(frame_split_across_chunks);
    RUN_TEST(several_frames_in_one_chunk);
    RUN_TEST(a_byte_at_a_time);
    RUN_TEST(longest_accepted_frame);
    RUN_TEST(one_byte_too_long_is_oversize_and_not_decoded);
    RUN_TEST(receiver_recovers_after_an_oversize_frame);
    RUN_TEST(overflow_stays_bounded_for_endless_input);
    RUN_TEST(null_callbacks_do_not_crash);
    return tinytest_summary();
}
