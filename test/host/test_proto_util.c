// Unit tests for components/device_proto/src/proto_util.c --
// proto_copy_cbor_text(), the function that closes Critical #2 from the
// security review: tinycbor's cbor_value_copy_text_string() reports
// success on a string that exactly fills the destination buffer, but
// leaves no room for a NUL terminator. The ECHO command and the
// unknown-verb ERR reply both then ran strlen()/the encoder over that
// un-terminated buffer, leaking adjacent stack memory back to the host
// over USB. See AGENTS.md, "Firmware mistakes to avoid", item 1.
//
// Build and run: see README.md (`make` in this directory).

#include "tinytest.h"
#include "proto_util.h"
#include "cbor.h"
#include <string.h>

// Encodes one CBOR text string of exactly `len` bytes (not NUL-based,
// so a length that happens to contain embedded structure is still
// exercised faithfully) into `wire`, and inits a parser/value over it.
static void encode_one_text(const char *text, size_t len, uint8_t *wire, size_t wire_cap, CborParser *parser,
                            CborValue *it)
{
    CborEncoder enc;
    cbor_encoder_init(&enc, wire, wire_cap, 0);
    CborError err = cbor_encode_text_string(&enc, text, len);
    CHECK_EQ(err, CborNoError);
    size_t used = cbor_encoder_get_buffer_size(&enc, wire);
    CborError perr = cbor_parser_init(wire, used, 0, parser, it);
    CHECK_EQ(perr, CborNoError);
}

/* ---------------------------------------------------------------------
 * The regression test: this is the literal vulnerability.
 * ------------------------------------------------------------------- */

TEST(exact_fit_string_is_rejected_not_silently_truncated)
{
    // A 16-byte string into a 16-byte buffer. tinycbor's own copy
    // succeeds here (verified below) with no NUL written -- the bug.
    // proto_copy_cbor_text() must reject this case outright.
    const char *payload = "AAAAAAAAAAAAAAAA"; // 16 'A's
    CHECK_EQ(strlen(payload), (size_t)16);

    uint8_t wire[64];
    CborParser parser;
    CborValue it;
    encode_one_text(payload, 16, wire, sizeof(wire), &parser, &it);

    // First, confirm tinycbor's raw behavior is what this fix assumes --
    // if a future tinycbor version changes this, this assertion (not
    // just the wrapper's behavior) should fail loudly.
    {
        char raw_buf[16];
        size_t raw_len = sizeof(raw_buf);
        CborValue raw_next;
        CborError raw_err = cbor_value_copy_text_string(&it, raw_buf, &raw_len, &raw_next);
        CHECK_EQ(raw_err, CborNoError); // tinycbor reports success...
        CHECK_EQ(raw_len, (size_t)16);  // ...and reports the full length used...
        // ...meaning raw_buf has no NUL terminator anywhere in its 16
        // bytes. We cannot safely strlen() it here (that is exactly the
        // bug), so the proof is raw_len == sizeof(raw_buf).
    }

    // Now the actual fix: must refuse, not trust the un-terminated copy.
    char dest[16];
    memset(dest, 0x55, sizeof(dest)); // poison, so a false positive would be visible
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == false);
}

/* ---------------------------------------------------------------------
 * Normal, boundary, and defensive cases.
 * ------------------------------------------------------------------- */

TEST(short_string_succeeds_and_is_terminated)
{
    uint8_t wire[64];
    CborParser parser;
    CborValue it;
    encode_one_text("hello", 5, wire, sizeof(wire), &parser, &it);

    char dest[16];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == true);
    CHECK_STREQ(dest, "hello");
}

TEST(string_one_byte_under_capacity_succeeds)
{
    // 15 bytes into a 16-byte buffer: exactly enough room for the NUL.
    const char *payload = "AAAAAAAAAAAAAAA"; // 15 'A's
    CHECK_EQ(strlen(payload), (size_t)15);

    uint8_t wire[64];
    CborParser parser;
    CborValue it;
    encode_one_text(payload, 15, wire, sizeof(wire), &parser, &it);

    char dest[16];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == true);
    CHECK_STREQ(dest, payload);
}

TEST(oversized_string_is_rejected)
{
    const char *payload = "this string is much longer than the destination buffer";
    uint8_t wire[128];
    CborParser parser;
    CborValue it;
    encode_one_text(payload, strlen(payload), wire, sizeof(wire), &parser, &it);

    char dest[8];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == false);
}

TEST(empty_string_succeeds)
{
    uint8_t wire[16];
    CborParser parser;
    CborValue it;
    encode_one_text("", 0, wire, sizeof(wire), &parser, &it);

    char dest[8];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == true);
    CHECK_STREQ(dest, "");
}

TEST(empty_string_into_one_byte_buffer_succeeds)
{
    // bufsz 1 means "room for the NUL and nothing else" -- a zero-length
    // string is the one input that should fit.
    uint8_t wire[16];
    CborParser parser;
    CborValue it;
    encode_one_text("", 0, wire, sizeof(wire), &parser, &it);

    char dest[1];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == true);
    CHECK_EQ(dest[0], '\0');
}

TEST(zero_capacity_buffer_is_rejected)
{
    uint8_t wire[16];
    CborParser parser;
    CborValue it;
    encode_one_text("", 0, wire, sizeof(wire), &parser, &it);

    char dest[1] = { 'X' };
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, 0, &next);
    CHECK(ok == false);
}

TEST(non_string_value_is_rejected)
{
    // A CFG/GET/SET/CAL handler always type-checks with
    // cbor_value_is_text_string() before calling this function in
    // practice, but the function itself must not misbehave if that
    // guard is ever missed -- it should fail cleanly, not crash or
    // read garbage.
    uint8_t wire[16];
    CborEncoder enc;
    cbor_encoder_init(&enc, wire, sizeof(wire), 0);
    cbor_encode_int(&enc, 42);
    size_t used = cbor_encoder_get_buffer_size(&enc, wire);
    CborParser parser;
    CborValue it;
    cbor_parser_init(wire, used, 0, &parser, &it);

    char dest[16];
    CborValue next;
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &next);
    CHECK(ok == false);
}

TEST(next_iterator_advances_past_the_string)
{
    // Build ["hi", 42] and confirm that after copying "hi", `next`
    // correctly points at the 42 -- the in-place-advance idiom
    // (passing the same CborValue* as both `it` and `next`) that
    // proto_cmd_dispatch() uses for the verb must work correctly.
    uint8_t wire[32];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, wire, sizeof(wire), 0);
    cbor_encoder_create_array(&enc, &arr, 2);
    cbor_encode_text_stringz(&arr, "hi");
    cbor_encode_int(&arr, 42);
    cbor_encoder_close_container(&enc, &arr);
    size_t used = cbor_encoder_get_buffer_size(&enc, wire);

    CborParser parser;
    CborValue top, it;
    cbor_parser_init(wire, used, 0, &parser, &top);
    cbor_value_enter_container(&top, &it);

    char dest[8];
    bool ok = proto_copy_cbor_text(&it, dest, sizeof(dest), &it); // same pointer in and out, as proto_cmd.c does
    CHECK(ok == true);
    CHECK_STREQ(dest, "hi");

    CHECK(cbor_value_is_integer(&it));
    int64_t v = 0;
    cbor_value_get_int64(&it, &v);
    CHECK_EQ(v, (int64_t)42);
}

/* ---- proto_parse_key_value_args ---- */

static bool parse_kv(const uint8_t *buf, size_t len, char *key, size_t ksz, bool *has, int64_t *val)
{
    CborParser p;
    CborValue top, it, v;
    if (cbor_parser_init(buf, len, 0, &p, &top) != CborNoError ||
        cbor_value_enter_container(&top, &it) != CborNoError) {
        return false;
    }
    if (!proto_parse_key_value_args(&it, key, ksz, has, &v)) {
        return false;
    }
    if (*has) {
        cbor_value_get_int64(&v, val);
    }
    return true;
}

TEST(key_only)
{
    const uint8_t b[] = { 0x81, 0x63, 'a', 'b', 'c' }; // ["abc"]
    char key[8];
    bool has = true;
    int64_t v = 0;
    CHECK(parse_kv(b, sizeof(b), key, sizeof(key), &has, &v));
    CHECK_STREQ(key, "abc");
    CHECK(!has);
}

TEST(key_and_value)
{
    const uint8_t b[] = { 0x82, 0x63, 'a', 'b', 'c', 0x18, 0x2A }; // ["abc", 42]
    char key[8];
    bool has = false;
    int64_t v = 0;
    CHECK(parse_kv(b, sizeof(b), key, sizeof(key), &has, &v));
    CHECK(has);
    CHECK_EQ(v, 42);
}

TEST(key_value_rejects_bad_shapes)
{
    char key[4];
    bool has;
    int64_t v;
    const uint8_t none[] = { 0x80 }; // []
    CHECK(!parse_kv(none, sizeof(none), key, sizeof(key), &has, &v));
    const uint8_t not_text[] = { 0x81, 0x05 }; // [5]
    CHECK(!parse_kv(not_text, sizeof(not_text), key, sizeof(key), &has, &v));
    const uint8_t two_values[] = { 0x83, 0x61, 'a', 0x01, 0x02 }; // ["a", 1, 2]
    CHECK(!parse_kv(two_values, sizeof(two_values), key, sizeof(key), &has, &v));
    const uint8_t too_long[] = { 0x81, 0x64, 'a', 'b', 'c', 'd' }; // ["abcd"] needs 5 bytes
    CHECK(!parse_kv(too_long, sizeof(too_long), key, sizeof(key), &has, &v));
}

int main(void)
{
    RUN_TEST(exact_fit_string_is_rejected_not_silently_truncated);
    RUN_TEST(short_string_succeeds_and_is_terminated);
    RUN_TEST(string_one_byte_under_capacity_succeeds);
    RUN_TEST(oversized_string_is_rejected);
    RUN_TEST(empty_string_succeeds);
    RUN_TEST(empty_string_into_one_byte_buffer_succeeds);
    RUN_TEST(zero_capacity_buffer_is_rejected);
    RUN_TEST(non_string_value_is_rejected);
    RUN_TEST(next_iterator_advances_past_the_string);
    RUN_TEST(key_only);
    RUN_TEST(key_and_value);
    RUN_TEST(key_value_rejects_bad_shapes);
    return tinytest_summary();
}
