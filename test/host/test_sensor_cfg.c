// Unit tests for components/sensor_core/src/sensor_cfg_logic.c.
#include "tinytest.h"
#include "sensor_cfg_logic.h"
#include <stdint.h>
#include <limits.h>

static const char *const kModes[] = { "SHORT", "LONG" };
static const sensor_cfgkey_def_t kUint = { "u", SENSOR_VAL_UINT, 10, 20, NULL, 0, 15, NULL };
static const sensor_cfgkey_def_t kInt = { "i", SENSOR_VAL_INT, -1024, 1023, NULL, 0, 0, NULL };
static const sensor_cfgkey_def_t kBool = { "b", SENSOR_VAL_BOOL, 0, 1, NULL, 0, 1, NULL };
static const sensor_cfgkey_def_t kEnum = { "e", SENSOR_VAL_ENUM, 0, 0, kModes, 2, 1, NULL };

/* ---- CBOR helpers --------------------------------------------------- */
static uint8_t g_buf[64];
static CborParser g_parser;

static CborValue parse_buf(size_t len, CborValue *it)
{
    CborError e = cbor_parser_init(g_buf, len, 0, &g_parser, it);
    (void)e;
    return *it;
}

static bool parse_uint(const sensor_cfgkey_def_t *d, uint64_t v, int32_t *out)
{
    CborEncoder enc;
    cbor_encoder_init(&enc, g_buf, sizeof(g_buf), 0);
    cbor_encode_uint(&enc, v);
    CborValue it;
    parse_buf(cbor_encoder_get_buffer_size(&enc, g_buf), &it);
    return scl_parse_cfg_value(&it, d, out);
}

static bool parse_int(const sensor_cfgkey_def_t *d, int64_t v, int32_t *out)
{
    CborEncoder enc;
    cbor_encoder_init(&enc, g_buf, sizeof(g_buf), 0);
    cbor_encode_int(&enc, v);
    CborValue it;
    parse_buf(cbor_encoder_get_buffer_size(&enc, g_buf), &it);
    return scl_parse_cfg_value(&it, d, out);
}

static bool parse_text(const sensor_cfgkey_def_t *d, const char *s, int32_t *out)
{
    CborEncoder enc;
    cbor_encoder_init(&enc, g_buf, sizeof(g_buf), 0);
    cbor_encode_text_stringz(&enc, s);
    CborValue it;
    parse_buf(cbor_encoder_get_buffer_size(&enc, g_buf), &it);
    return scl_parse_cfg_value(&it, d, out);
}

static bool parse_bool(const sensor_cfgkey_def_t *d, bool b, int32_t *out)
{
    CborEncoder enc;
    cbor_encoder_init(&enc, g_buf, sizeof(g_buf), 0);
    cbor_encode_boolean(&enc, b);
    CborValue it;
    parse_buf(cbor_encoder_get_buffer_size(&enc, g_buf), &it);
    return scl_parse_cfg_value(&it, d, out);
}

/* ---- parse_cfg_value ------------------------------------------------ */

TEST(uint_boundaries)
{
    int32_t v = -1;
    CHECK(parse_uint(&kUint, 10, &v) && v == 10);
    CHECK(parse_uint(&kUint, 20, &v) && v == 20);
    CHECK(!parse_uint(&kUint, 9, &v));
    CHECK(!parse_uint(&kUint, 21, &v));
}

TEST(int_negative_boundaries)
{
    int32_t v = 0;
    CHECK(parse_int(&kInt, -1024, &v) && v == -1024);
    CHECK(parse_int(&kInt, 1023, &v) && v == 1023);
    CHECK(!parse_int(&kInt, -1025, &v));
    CHECK(!parse_int(&kInt, 1024, &v));
}

TEST(oversize_integers_do_not_wrap_into_range)
{
    // Bug 1: tinycbor's plain cbor_value_get_int64() wraps 2^64-1 to -1 and
    // -2^64 to 0. Both then looked like legal values for this key.
    CborEncoder enc;
    cbor_encoder_init(&enc, g_buf, sizeof(g_buf), 0);
    cbor_encode_uint(&enc, UINT64_MAX);
    CborValue it;
    parse_buf(cbor_encoder_get_buffer_size(&enc, g_buf), &it);
    int64_t wrapped = 12345;
    cbor_value_get_int64(&it, &wrapped);
    CHECK_EQ(wrapped, -1); // the raw library call really does wrap

    int32_t v = 77;
    CHECK(!parse_uint(&kInt, UINT64_MAX, &v));
    CHECK(!parse_uint(&kInt, (uint64_t)INT64_MAX + 1, &v));
    CHECK(!parse_uint(&kUint, UINT64_MAX, &v));
    CHECK_EQ(v, 77); // *out untouched on failure

    // Negative below INT64_MIN: encoded by hand as major type 1, value 2^64-1.
    uint8_t neg[] = { 0x3B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(g_buf, neg, sizeof(neg));
    parse_buf(sizeof(neg), &it);
    CHECK(!scl_parse_cfg_value(&it, &kInt, &v));
}

TEST(wrong_cbor_type_rejected_for_each_key_type)
{
    int32_t v;
    CHECK(!parse_text(&kUint, "15", &v));
    CHECK(!parse_bool(&kUint, true, &v));
    CHECK(!parse_uint(&kBool, 1, &v));
    CHECK(!parse_uint(&kEnum, 0, &v));
    CHECK(!parse_text(&kBool, "true", &v));
}

TEST(bool_values)
{
    int32_t v = -1;
    CHECK(parse_bool(&kBool, true, &v) && v == 1);
    CHECK(parse_bool(&kBool, false, &v) && v == 0);
}

TEST(enum_names)
{
    int32_t v = -1;
    CHECK(parse_text(&kEnum, "SHORT", &v) && v == 0);
    CHECK(parse_text(&kEnum, "LONG", &v) && v == 1);
    CHECK(!parse_text(&kEnum, "long", &v));
    CHECK(!parse_text(&kEnum, "", &v));
    CHECK(!parse_text(&kEnum, "LONGER", &v));
}

TEST(enum_name_length_limit_23_ok_24_rejected)
{
    char name[40];
    int32_t v;
    memset(name, 'x', 23);
    name[23] = '\0';
    CHECK(!parse_text(&kEnum, name, &v)); // fits the buffer, not in list
    memset(name, 'x', 24);
    name[24] = '\0';
    CHECK(!parse_text(&kEnum, name, &v)); // exact fit: rejected by the copy
    // The name that is in the list still works.
    CHECK(parse_text(&kEnum, "SHORT", &v));
}

/* ---- range and definition checks ------------------------------------ */

TEST(value_in_range_by_type)
{
    CHECK(scl_value_in_range(&kUint, 10) && scl_value_in_range(&kUint, 20));
    CHECK(!scl_value_in_range(&kUint, 9) && !scl_value_in_range(&kUint, 21));
    CHECK(scl_value_in_range(&kBool, 0) && scl_value_in_range(&kBool, 1));
    CHECK(!scl_value_in_range(&kBool, 2) && !scl_value_in_range(&kBool, -1));
    CHECK(scl_value_in_range(&kEnum, 1) && !scl_value_in_range(&kEnum, 2));
    CHECK(!scl_value_in_range(&kEnum, 268435455));
}

TEST(cfgkey_def_validation)
{
    CHECK(scl_cfgkey_def_valid(&kUint) && scl_cfgkey_def_valid(&kInt));
    CHECK(scl_cfgkey_def_valid(&kBool) && scl_cfgkey_def_valid(&kEnum));
    sensor_cfgkey_def_t d = kEnum;
    d.default_value = 2;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kEnum;
    d.enum_count = 0;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kEnum;
    d.enum_values = NULL;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kUint;
    d.min = 30;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kUint;
    d.default_value = 99;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kBool;
    d.default_value = 2;
    CHECK(!scl_cfgkey_def_valid(&d));
    d = kUint;
    d.name = NULL;
    CHECK(!scl_cfgkey_def_valid(&d));
    CHECK(!scl_cfgkey_def_valid(NULL));
}

TEST(unknown_key_type_is_invalid)
{
    sensor_cfgkey_def_t d = kUint;
    d.type = (sensor_val_type_t)99;
    CHECK(!scl_cfgkey_def_valid(&d));
}

TEST(enum_text_is_bounds_checked)
{
    // Bug 2: a chip register can hold 2 or 3 while the enum has 2 names.
    CHECK_STREQ(scl_enum_text(&kEnum, 0), "SHORT");
    CHECK_STREQ(scl_enum_text(&kEnum, 1), "LONG");
    CHECK(scl_enum_text(&kEnum, 2) == NULL);
    CHECK(scl_enum_text(&kEnum, 3) == NULL);
    CHECK(scl_enum_text(&kEnum, -1) == NULL);
    CHECK(scl_enum_text(&kUint, 0) == NULL);
}

TEST(join_enum_values)
{
    char out[32];
    scl_join_enum_values(&kEnum, out, sizeof(out));
    CHECK_STREQ(out, "SHORT,LONG");
    scl_join_enum_values(&kEnum, out, 8); // cut, still terminated
    CHECK_EQ(strlen(out), 7u);
    CHECK(out[7] == '\0');
    scl_join_enum_values(&kEnum, out, 1);
    CHECK_EQ(out[0], '\0');
    out[0] = 'X';
    scl_join_enum_values(&kEnum, out, 0); // nothing written at all
    CHECK_EQ(out[0], 'X');
    sensor_cfgkey_def_t one = kEnum;
    one.enum_count = 1;
    scl_join_enum_values(&one, out, sizeof(out));
    CHECK_STREQ(out, "SHORT");
    sensor_cfgkey_def_t none = kEnum;
    none.enum_count = 0;
    scl_join_enum_values(&none, out, sizeof(out));
    CHECK_STREQ(out, "");
}

/* ---- the saved record ---------------------------------------------- */

TEST(blob_round_trip_and_layout)
{
    sensor_cfgkey_def_t defs[3] = { kUint, kBool, kEnum };
    int32_t in[3] = { 12, 0, 1 };
    uint8_t buf[16];
    size_t n = scl_blob_pack(in, 3, 1, buf);
    CHECK_EQ(n, 13u);
    CHECK_EQ(n, scl_blob_len(3));
    CHECK_EQ(buf[0], 1);
    int32_t out[3];
    size_t replaced = 99;
    CHECK(scl_blob_unpack(buf, n, 1, defs, 3, out, &replaced));
    CHECK(out[0] == 12 && out[1] == 0 && out[2] == 1);
    CHECK_EQ(replaced, 0u);
}

TEST(blob_wrong_version_or_length_gives_all_defaults)
{
    sensor_cfgkey_def_t defs[2] = { kUint, kEnum };
    int32_t in[2] = { 12, 0 };
    uint8_t buf[16];
    size_t n = scl_blob_pack(in, 2, 1, buf);
    int32_t out[2];
    CHECK(!scl_blob_unpack(buf, n, 2, defs, 2, out, NULL)); // version
    CHECK(out[0] == 15 && out[1] == 1);
    CHECK(!scl_blob_unpack(buf, n - 1, 1, defs, 2, out, NULL)); // too short
    CHECK(!scl_blob_unpack(buf, n + 1, 1, defs, 2, out, NULL)); // too long
    CHECK(out[0] == 15 && out[1] == 1);
    CHECK(!scl_blob_unpack(NULL, 0, 1, defs, 2, out, NULL));
}

TEST(blob_one_bad_value_defaults_only_that_key)
{
    sensor_cfgkey_def_t defs[3] = { kUint, kBool, kEnum };
    int32_t in[3] = { 12, 7 /* bad bool */, 0 };
    uint8_t buf[16];
    size_t n = scl_blob_pack(in, 3, 1, buf);
    int32_t out[3];
    size_t replaced = 0;
    CHECK(scl_blob_unpack(buf, n, 1, defs, 3, out, &replaced));
    CHECK_EQ(out[0], 12);
    CHECK_EQ(out[1], 1); // bool default
    CHECK_EQ(out[2], 0);
    CHECK_EQ(replaced, 1u);
}

TEST(blob_garbage_enum_value_is_replaced)
{
    sensor_cfgkey_def_t defs[1] = { kEnum };
    int32_t in[1] = { 268435455 };
    uint8_t buf[8];
    size_t n = scl_blob_pack(in, 1, 1, buf);
    int32_t out[1];
    CHECK(scl_blob_unpack(buf, n, 1, defs, 1, out, NULL));
    CHECK_EQ(out[0], 1);
}

TEST(blob_zero_keys)
{
    uint8_t buf[2];
    size_t n = scl_blob_pack(NULL, 0, 1, buf);
    CHECK_EQ(n, 1u);
    CHECK(scl_blob_unpack(buf, n, 1, NULL, 0, NULL, NULL));
}

/* ---- names ---------------------------------------------------------- */

TEST(scaps_filter)
{
    CHECK(scl_scaps_filter_match("tof0.dist_mode", "tof0"));
    CHECK(!scl_scaps_filter_match("tof01.dist_mode", "tof0"));
    CHECK(!scl_scaps_filter_match("tof0", "tof0"));
    CHECK(!scl_scaps_filter_match("tof0.x", "tof"));
    CHECK(!scl_scaps_filter_match("tof0.x", ""));
}

TEST(split_action_key)
{
    char k1[] = "tof0.offset";
    const char *a;
    CHECK(scl_split_action_key(k1, &a));
    CHECK_STREQ(k1, "tof0");
    CHECK_STREQ(a, "offset");
    char k2[] = "a.b.c";
    CHECK(scl_split_action_key(k2, &a));
    CHECK_STREQ(k2, "a.b");
    CHECK_STREQ(a, "c");
    char k3[] = "nodot";
    CHECK(!scl_split_action_key(k3, &a));
    char k4[] = "tof0.";
    CHECK(!scl_split_action_key(k4, &a));
    char k5[] = ".offset";
    CHECK(!scl_split_action_key(k5, &a));
    char k6[] = "";
    CHECK(!scl_split_action_key(k6, &a));
}

/* ---- instance state rules ------------------------------------------ */

TEST(state_gates_table)
{
    struct {
        sensor_inst_state_t s;
        bool op, poll, write;
        const char *text;
    } t[] = {
        { SENSOR_INST_PROBING, false, false, false, "fault" },
        { SENSOR_INST_PROBE_FAILED, false, false, false, "fault" },
        { SENSOR_INST_INIT_FAILED, false, false, false, "init_failed" },
        { SENSOR_INST_START_FAILED, false, false, false, "start_failed" },
        { SENSOR_INST_RUNNING, true, true, true, "ok" },
        { SENSOR_INST_BUSY, true, false, false, "busy" },
        { SENSOR_INST_FAULT, false, false, false, "fault" },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        CHECK_EQ(scl_state_is_operational(t[i].s), t[i].op);
        CHECK_EQ(scl_state_can_poll(t[i].s), t[i].poll);
        CHECK_EQ(scl_state_can_write_cfg(t[i].s), t[i].write);
        CHECK_STREQ(scl_state_text(t[i].s), t[i].text);
    }
    CHECK_STREQ(scl_state_text((sensor_inst_state_t)99), "fault");
    CHECK_EQ(SENSOR_INST_PROBING, 0);
}

TEST(lifecycle_outcomes)
{
    CHECK_EQ(scl_lifecycle_outcome(false, true, true), SENSOR_INST_PROBE_FAILED);
    CHECK_EQ(scl_lifecycle_outcome(false, false, false), SENSOR_INST_PROBE_FAILED);
    CHECK_EQ(scl_lifecycle_outcome(true, false, true), SENSOR_INST_INIT_FAILED);
    CHECK_EQ(scl_lifecycle_outcome(true, true, false), SENSOR_INST_START_FAILED);
    CHECK_EQ(scl_lifecycle_outcome(true, true, true), SENSOR_INST_RUNNING);
}

TEST(cal_finish_does_not_overwrite_a_fault)
{
    CHECK_EQ(scl_cal_finish_state(SENSOR_INST_BUSY), SENSOR_INST_RUNNING);
    CHECK_EQ(scl_cal_finish_state(SENSOR_INST_FAULT), SENSOR_INST_FAULT);
    CHECK_EQ(scl_cal_finish_state(SENSOR_INST_RUNNING), SENSOR_INST_RUNNING);
}

TEST(cal_error_codes)
{
    CHECK_STREQ(scl_cal_error_code(0x102, 0x102, 0x105), "ERANGE");
    CHECK_STREQ(scl_cal_error_code(0x105, 0x102, 0x105), "ENOCH");
    CHECK_STREQ(scl_cal_error_code(-1, 0x102, 0x105), "EINTERNAL");
    CHECK_STREQ(scl_cal_error_code(0x107, 0x102, 0x105), "EINTERNAL");
}

int main(void)
{
    RUN_TEST(uint_boundaries);
    RUN_TEST(int_negative_boundaries);
    RUN_TEST(oversize_integers_do_not_wrap_into_range);
    RUN_TEST(wrong_cbor_type_rejected_for_each_key_type);
    RUN_TEST(bool_values);
    RUN_TEST(enum_names);
    RUN_TEST(enum_name_length_limit_23_ok_24_rejected);
    RUN_TEST(value_in_range_by_type);
    RUN_TEST(cfgkey_def_validation);
    RUN_TEST(unknown_key_type_is_invalid);
    RUN_TEST(enum_text_is_bounds_checked);
    RUN_TEST(join_enum_values);
    RUN_TEST(blob_round_trip_and_layout);
    RUN_TEST(blob_wrong_version_or_length_gives_all_defaults);
    RUN_TEST(blob_one_bad_value_defaults_only_that_key);
    RUN_TEST(blob_garbage_enum_value_is_replaced);
    RUN_TEST(blob_zero_keys);
    RUN_TEST(scaps_filter);
    RUN_TEST(split_action_key);
    RUN_TEST(state_gates_table);
    RUN_TEST(lifecycle_outcomes);
    RUN_TEST(cal_finish_does_not_overwrite_a_fault);
    RUN_TEST(cal_error_codes);
    return tinytest_summary();
}
