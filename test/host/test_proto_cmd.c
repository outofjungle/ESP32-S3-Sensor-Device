// Unit tests for proto_reply.c and proto_cmd.c. Both files are included
// directly so the tests can reset their static state. A fake
// usb_cdc_send_response_frame() captures every reply for decoding.
#include "tinytest.h"
#include "../../components/device_proto/src/channel_registry.c"
#include "../../components/device_proto/src/device_config.c"
#include "../../components/device_proto/src/proto_reply.c"
#include "../../components/device_proto/src/proto_cmd.c"
#include <stdint.h>

/* ---- fakes for usb_cdc.c and device_stream.c ------------------------ */

#define MAX_FRAMES 48
static uint8_t g_frames[MAX_FRAMES][320];
static size_t g_frame_len[MAX_FRAMES];
static int g_frame_count;
static int g_crc_errors;
static int g_period_applied;

bool usb_cdc_send_response_frame(const uint8_t *p, size_t len)
{
    if (g_frame_count < MAX_FRAMES && len <= sizeof(g_frames[0])) {
        memcpy(g_frames[g_frame_count], p, len);
        g_frame_len[g_frame_count++] = len;
    }
    return true;
}
const char *usb_cdc_get_serial(void)
{
    return "AABBCCDDEEFF";
}
void usb_cdc_note_crc_error(void)
{
    g_crc_errors++;
}
void usb_cdc_get_stats(usb_cdc_stats_t *o)
{
    o->frames_rx = 1;
    o->frames_tx = 2;
    o->crc_errors = 3;
    o->samples_dropped = 4;
    o->log_lines_dropped = 5;
}
void device_stream_apply_period(void)
{
    g_period_applied++;
}

/* ---- frame helpers --------------------------------------------------- */

static void reset(void)
{
    g_frame_count = 0;
    g_crc_errors = 0;
    g_period_applied = 0;
    g_restart_calls = 0;
    g_delay_ms_total = 0;
    s_count = 0;
    s_command_count = 0;
    s_on_get_hook = NULL;
    dc_set_defaults(&s_cfg);
    g_nvs_has_blob = 0;
    proto_cmd_init("Test Device");
}

// Parses frame i and positions *it at its item `idx` (0 = kind).
static bool frame_item(int i, size_t idx, CborValue *out, CborParser *parser)
{
    CborValue top, it;
    if (cbor_parser_init(g_frames[i], g_frame_len[i], 0, parser, &top) != CborNoError)
        return false;
    if (!cbor_value_is_array(&top) || cbor_value_enter_container(&top, &it) != CborNoError)
        return false;
    for (size_t k = 0; k < idx; k++) {
        if (cbor_value_at_end(&it) || cbor_value_advance(&it) != CborNoError)
            return false;
    }
    if (cbor_value_at_end(&it))
        return false;
    *out = it;
    return true;
}
static int64_t f_int(int i, size_t idx)
{
    CborParser p;
    CborValue v;
    int64_t r = -99999;
    if (frame_item(i, idx, &v, &p) && cbor_value_is_integer(&v))
        cbor_value_get_int64(&v, &r);
    return r;
}
static bool f_text(int i, size_t idx, char *out, size_t cap)
{
    CborParser p;
    CborValue v;
    if (!frame_item(i, idx, &v, &p) || !cbor_value_is_text_string(&v))
        return false;
    CborValue nx;
    return proto_copy_cbor_text(&v, out, cap, &nx);
}
static size_t f_items(int i)
{
    CborParser p;
    CborValue top;
    size_t n = 0;
    cbor_parser_init(g_frames[i], g_frame_len[i], 0, &p, &top);
    cbor_value_get_array_length(&top, &n);
    return n;
}
static bool f_is_err(int i, const char *code)
{
    char t[16];
    return f_int(i, 0) == FRAME_KIND_ERR && f_text(i, 3, t, sizeof(t)) && strcmp(t, code) == 0;
}
static bool f_is_ok(int i)
{
    return f_int(i, 0) == FRAME_KIND_OK;
}

/* ---- command builder -------------------------------------------------- */

typedef struct {
    uint8_t buf[300];
    CborEncoder enc, arr;
} cmd_t;
static void cmd_begin(cmd_t *c, int64_t kind, int64_t seq, const char *verb, size_t items)
{
    cbor_encoder_init(&c->enc, c->buf, sizeof(c->buf), 0);
    cbor_encoder_create_array(&c->enc, &c->arr, items);
    cbor_encode_int(&c->arr, kind);
    cbor_encode_int(&c->arr, seq);
    cbor_encode_text_stringz(&c->arr, verb);
}
static void cmd_send(cmd_t *c)
{
    cbor_encoder_close_container(&c->enc, &c->arr);
    proto_cmd_dispatch(c->buf, cbor_encoder_get_buffer_size(&c->enc, c->buf));
}
static void run0(const char *verb, int64_t seq)
{
    cmd_t c;
    cmd_begin(&c, 0, seq, verb, 3);
    cmd_send(&c);
}
static void run_text(const char *verb, const char *arg)
{
    cmd_t c;
    cmd_begin(&c, 0, 7, verb, 4);
    cbor_encode_text_stringz(&c.arr, arg);
    cmd_send(&c);
}
static void run_text_int(const char *verb, const char *key, int64_t v)
{
    cmd_t c;
    cmd_begin(&c, 0, 7, verb, 5);
    cbor_encode_text_stringz(&c.arr, key);
    cbor_encode_int(&c.arr, v);
    cmd_send(&c);
}
static void run_text_text(const char *verb, const char *key, const char *v)
{
    cmd_t c;
    cmd_begin(&c, 0, 7, verb, 5);
    cbor_encode_text_stringz(&c.arr, key);
    cbor_encode_text_stringz(&c.arr, v);
    cmd_send(&c);
}

static esp_err_t get_const(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    return cbor_encode_uint(enc, 42) == CborNoError ? ESP_OK : ESP_FAIL;
}
static esp_err_t get_nothing(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    (void)enc;
    return ESP_FAIL;
}
static int set_ok(void *ctx, CborValue *v)
{
    (void)ctx;
    (void)v;
    return 0;
}
static int set_range(void *ctx, CborValue *v)
{
    (void)ctx;
    (void)v;
    return ERR_RANGE;
}
static int set_arg(void *ctx, CborValue *v)
{
    (void)ctx;
    (void)v;
    return ERR_ARG;
}

/* ---- proto_reply ------------------------------------------------------- */

TEST(reply_shapes)
{
    reset();
    proto_reply_ok0(5, "PING");
    CHECK_EQ(f_items(0), 3u);
    CHECK_EQ(f_int(0, 0), FRAME_KIND_OK);
    CHECK_EQ(f_int(0, 1), 5);
    proto_reply_ok_uint(UINT32_MAX, "X", UINT64_MAX);
    CHECK_EQ(f_items(1), 4u);
    CHECK_EQ(f_int(1, 1), (int64_t)UINT32_MAX);
    proto_reply_ok_int(1, "X", INT64_MIN);
    CHECK_EQ(f_int(2, 3), INT64_MIN);
    proto_reply_ok_bool(1, "X", true);
    CHECK_EQ(f_items(3), 4u);
    proto_reply_ok_text(1, "X", "hello");
    char t[16];
    CHECK(f_text(4, 3, t, sizeof(t)));
    CHECK_STREQ(t, "hello");
    proto_reply_err(9, "GET", "ENOCH");
    CHECK(f_is_err(5, "ENOCH"));
    CHECK_EQ(f_int(5, 1), 9);
}

TEST(oversize_reply_becomes_an_error_not_a_broken_frame)
{
    // A text value too big for the transmit buffer must not go out as a
    // truncated or garbage frame; the host gets a well-formed EINTERNAL.
    reset();
    char big[PROTO_MAX_PAYLOAD + 40];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    proto_reply_ok_text(11, "ECHO", big);
    CHECK_EQ(g_frame_count, 1);
    CHECK(f_is_err(0, "EINTERNAL"));
    CHECK_EQ(f_int(0, 1), 11);
    char verb[16];
    CHECK(f_text(0, 2, verb, sizeof(verb)));
    CHECK_STREQ(verb, "ECHO");
    CHECK(g_frame_len[0] <= PROTO_MAX_PAYLOAD);
}

TEST(a_reply_that_exactly_fits_is_sent_whole)
{
    reset();
    char text[200];
    memset(text, 'b', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    proto_reply_ok_text(1, "ECHO", text);
    CHECK(f_is_ok(0));
    char got[256];
    CHECK(f_text(0, 3, got, sizeof(got)));
    CHECK_STREQ(got, text);
}

/* ---- dispatch ---------------------------------------------------------- */

TEST(ping_id_stat_basic)
{
    reset();
    run0("PING", 1);
    CHECK(f_is_ok(0));
    CHECK_EQ(f_int(0, 1), 1);
    run0("ID", 2);
    char t[40];
    CHECK(f_text(1, 3, t, sizeof(t)));
    CHECK_STREQ(t, "Test Device");
    CHECK(f_text(1, 4, t, sizeof(t)));
    CHECK_STREQ(t, "test-1.0");
    CHECK(f_text(1, 5, t, sizeof(t)));
    CHECK_STREQ(t, "AABBCCDDEEFF");
    run0("STAT", 3);
    CHECK_EQ(f_items(2), 9u);
    CHECK_EQ(f_int(2, 3), 123456);
}

TEST(extra_arguments_are_rejected)
{
    reset();
    const char *verbs[] = { "PING", "ID", "CAPS", "STAT", "RST", "START", "STOP", "SAVE", "LOAD" };
    for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        g_frame_count = 0;
        cmd_t c;
        cmd_begin(&c, 0, 4, verbs[i], 4);
        cbor_encode_int(&c.arr, 1);
        cmd_send(&c);
        CHECK_EQ(g_frame_count, 1);
        CHECK(f_is_err(0, "EARG"));
    }
    CHECK_EQ(g_restart_calls, 0); // RST with an argument must not restart
}

TEST(rst_replies_then_restarts)
{
    reset();
    run0("RST", 1);
    CHECK(f_is_ok(0));
    CHECK_EQ(g_restart_calls, 1);
}

TEST(unknown_verb_and_wrong_kind)
{
    reset();
    run0("NOPE", 3);
    CHECK(f_is_err(0, "ECMD"));
    CHECK_EQ(f_int(0, 1), 3);
    cmd_t c;
    cmd_begin(&c, 1, 4, "PING", 3);
    cmd_send(&c);
    CHECK(f_is_err(1, "EFRAME"));
}

TEST(verb_of_15_chars_is_unknown_16_is_a_frame_error)
{
    reset();
    run0("ABCDEFGHIJKLMNO", 1); // 15 chars: fits, simply unknown
    CHECK(f_is_err(0, "ECMD"));
    char v[16];
    CHECK(f_text(0, 2, v, sizeof(v)));
    CHECK_STREQ(v, "ABCDEFGHIJKLMNO");
    run0("ABCDEFGHIJKLMNOP", 2); // 16 chars: exact fit, rejected
    CHECK(f_is_err(1, "EFRAME"));
    CHECK(f_text(1, 2, v, sizeof(v)));
    CHECK_STREQ(v, ""); // nothing leaked
}

TEST(malformed_frames_are_counted_not_answered)
{
    reset();
    uint8_t not_array[] = { 0x01 };
    proto_cmd_dispatch(not_array, 1);
    uint8_t too_short[] = { 0x82, 0x00, 0x01 }; // [0, 1]
    proto_cmd_dispatch(too_short, sizeof(too_short));
    uint8_t indefinite[] = { 0x9F, 0x00, 0x01, 0x64, 'P', 'I', 'N', 'G', 0xFF };
    proto_cmd_dispatch(indefinite, sizeof(indefinite));
    uint8_t text_kind[] = { 0x83, 0x61, 'a', 0x01, 0x64, 'P', 'I', 'N', 'G' };
    proto_cmd_dispatch(text_kind, sizeof(text_kind));
    uint8_t text_seq[] = { 0x83, 0x00, 0x61, 'a', 0x64, 'P', 'I', 'N', 'G' };
    proto_cmd_dispatch(text_seq, sizeof(text_seq));
    proto_cmd_dispatch(not_array, 0); // empty
    CHECK_EQ(g_frame_count, 0);
    CHECK(g_crc_errors >= 6);
    // A cut-off verb comes after a readable sequence number, so the host
    // is told (EFRAME) rather than left waiting.
    uint8_t truncated[] = { 0x83, 0x00, 0x01, 0x64, 'P' };
    proto_cmd_dispatch(truncated, sizeof(truncated));
    CHECK_EQ(g_frame_count, 1);
    CHECK(f_is_err(0, "EFRAME"));
}

TEST(sequence_number_out_of_range_is_not_trusted)
{
    // docs/05: the sequence number is 0..65535. Anything else cannot be
    // echoed back truthfully, so it is dropped like an unreadable frame.
    reset();
    run0("PING", 65535);
    CHECK(f_is_ok(0));
    CHECK_EQ(f_int(0, 1), 65535);
    int before = g_crc_errors;
    run0("PING", 65536);
    run0("PING", -1);
    run0("PING", (int64_t)1 << 32 | 5);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_crc_errors, before + 3);
}

/* ---- ECHO, GET, SET, CAPS ------------------------------------------------- */

TEST(echo_127_ok_128_rejected)
{
    reset();
    char s[200];
    memset(s, 'e', 127);
    s[127] = '\0';
    run_text("ECHO", s);
    CHECK(f_is_ok(0));
    char got[200];
    CHECK(f_text(0, 3, got, sizeof(got)));
    CHECK_STREQ(got, s);
    memset(s, 'e', 128);
    s[128] = '\0';
    run_text("ECHO", s);
    CHECK(f_is_err(1, "EARG"));
    cmd_t c;
    cmd_begin(&c, 0, 1, "ECHO", 4);
    cbor_encode_int(&c.arr, 5);
    cmd_send(&c);
    CHECK(f_is_err(2, "EARG"));
}

TEST(get_single_channel_and_missing)
{
    reset();
    channel_registry_register("a", "uint", "RO", NULL, get_const, NULL);
    run_text("GET", "a");
    CHECK(f_is_ok(0));
    CHECK_EQ(f_int(0, 3), 42);
    run_text("GET", "zzz");
    CHECK(f_is_err(1, "ENOCH"));
}

TEST(a_get_callback_that_encodes_nothing_gives_an_error_frame)
{
    // The array was declared with one more item than was written. That used
    // to go out as a frame that decodes fine at the CRC level but not as CBOR.
    reset();
    channel_registry_register("broken", "uint", "RO", NULL, get_nothing, NULL);
    run_text("GET", "broken");
    CHECK_EQ(g_frame_count, 1);
    CHECK(f_is_err(0, "EINTERNAL"));
}

TEST(get_name_length_limit)
{
    reset();
    char n[64];
    memset(n, 'n', 39);
    n[39] = '\0';
    run_text("GET", n); // 39 chars fits; just not registered
    CHECK(f_is_err(0, "ENOCH"));
    memset(n, 'n', 40);
    n[40] = '\0';
    run_text("GET", n); // 40 chars: argument error
    CHECK(f_is_err(1, "EARG"));
}

static int hook_calls;
static void count_hook(void)
{
    hook_calls++;
}

TEST(get_all_sends_rows_then_count_and_runs_the_hook)
{
    reset();
    hook_calls = 0;
    proto_cmd_set_on_get_hook(count_hook);
    channel_registry_register("a", "uint", "RO", NULL, get_const, NULL);
    channel_registry_register("b", "uint", "RO", NULL, get_const, NULL);
    run_text("GET", "*");
    CHECK_EQ(g_frame_count, 3);
    CHECK_EQ(f_int(0, 0), FRAME_KIND_ROW);
    CHECK_EQ(f_int(1, 0), FRAME_KIND_ROW);
    CHECK(f_is_ok(2));
    CHECK_EQ(f_int(2, 3), 2);
    CHECK_EQ(hook_calls, 1);
}

TEST(get_all_with_empty_registry)
{
    reset();
    run_text("GET", "*");
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(f_int(0, 3), 0);
}

TEST(set_paths)
{
    reset();
    channel_registry_register("rw", "uint", "RW", NULL, get_const, set_ok);
    channel_registry_register("ro", "uint", "RO", NULL, get_const, NULL);
    channel_registry_register("bad", "uint", "RW", NULL, get_const, set_range);
    channel_registry_register("arg", "uint", "RW", NULL, get_const, set_arg);
    run_text_int("SET", "rw", 1);
    CHECK(f_is_ok(0));
    run_text_int("SET", "ro", 1);
    CHECK(f_is_err(1, "EACCESS"));
    run_text_int("SET", "bad", 1);
    CHECK(f_is_err(2, "ERANGE"));
    run_text_int("SET", "arg", 1);
    CHECK(f_is_err(3, "EINTERNAL"));
    run_text_int("SET", "nope", 1);
    CHECK(f_is_err(4, "ENOCH"));
    run_text("SET", "rw");
    CHECK(f_is_err(5, "EARG")); // no value
    cmd_t c;
    cmd_begin(&c, 0, 1, "SET", 6);
    cbor_encode_text_stringz(&c.arr, "rw");
    cbor_encode_int(&c.arr, 1);
    cbor_encode_int(&c.arr, 2);
    cmd_send(&c);
    CHECK(f_is_err(6, "EARG")); // two values
}

TEST(caps_lists_every_channel)
{
    reset();
    channel_registry_register("a", "uint", "RO", NULL, get_const, NULL);
    channel_registry_register("b", "bool", "RW", NULL, get_const, set_ok);
    run0("CAPS", 1);
    CHECK_EQ(g_frame_count, 3);
    char t[16];
    CHECK(f_text(1, 4, t, sizeof(t)));
    CHECK_STREQ(t, "bool");
    CHECK(f_text(1, 5, t, sizeof(t)));
    CHECK_STREQ(t, "RW");
    CHECK_EQ(f_int(2, 3), 2);
}

/* ---- CFG --------------------------------------------------------------- */

TEST(cfg_period_us)
{
    reset();
    run_text("CFG", "PERIOD_US");
    CHECK(f_is_ok(0));
    CHECK_EQ(f_int(0, 3), 100000);
    run_text_int("CFG", "PERIOD_US", 20000);
    CHECK(f_is_ok(1));
    CHECK_EQ(f_int(1, 3), 20000);
    CHECK_EQ(g_period_applied, 1);
    run_text_int("CFG", "PERIOD_US", -1);
    CHECK(f_is_err(2, "EARG"));
    run_text_int("CFG", "PERIOD_US", 999);
    CHECK(f_is_err(3, "ERANGE"));
    run_text_int("CFG", "PERIOD_US", 3600000000LL);
    CHECK(f_is_ok(4));
    run_text_int("CFG", "PERIOD_US", 3600000001LL);
    CHECK(f_is_err(5, "EARG"));
    run_text_text("CFG", "PERIOD_US", "fast");
    CHECK(f_is_err(6, "EARG"));
    CHECK_EQ(g_period_applied, 2); // only successful sets apply it
}

TEST(cfg_batch)
{
    reset();
    run_text_int("CFG", "BATCH", 8);
    CHECK(f_is_ok(0));
    CHECK_EQ(f_int(0, 3), 8);
    run_text_int("CFG", "BATCH", 33);
    CHECK(f_is_err(1, "ERANGE"));
    run_text_int("CFG", "BATCH", 0);
    CHECK(f_is_err(2, "ERANGE"));
    run_text_int("CFG", "BATCH", 65536);
    CHECK(f_is_err(3, "EARG"));
    run_text_int("CFG", "BATCH", -1);
    CHECK(f_is_err(4, "EARG"));
}

TEST(cfg_text_keys)
{
    reset();
    run_text_text("CFG", "MODE", "BOTH");
    CHECK(f_is_ok(0));
    char t[16];
    CHECK(f_text(0, 3, t, sizeof(t)));
    CHECK_STREQ(t, "BOTH");
    run_text_text("CFG", "MODE", "WHENEVER");
    CHECK(f_is_err(1, "ERANGE"));
    run_text_text("CFG", "MODE", "0123456789ABCDEF");
    CHECK(f_is_err(2, "EARG")); // 16 chars
    run_text_int("CFG", "MODE", 3);
    CHECK(f_is_err(3, "EARG"));
    run_text_text("CFG", "ENCODING", "CBOR");
    CHECK(f_is_ok(4));
    run_text_text("CFG", "ENCODING", "JSON");
    CHECK(f_is_err(5, "ERANGE"));
    run_text_text("CFG", "LED_MODE", "MANUAL");
    CHECK(f_is_ok(6));
    run_text_text("CFG", "LED_MODE", "STATUSXYZ");
    CHECK(f_is_err(7, "EARG")); // 9 bytes: too long
    run_text_text("CFG", "LED_MODE", "NOPE");
    CHECK(f_is_err(8, "ERANGE"));
}

TEST(cfg_unknown_or_malformed)
{
    reset();
    run_text("CFG", "NOPE");
    CHECK(f_is_err(0, "EARG"));
    run_text("CFG", "0123456789ABCDE");
    CHECK(f_is_err(1, "EARG")); // 15 chars: unknown key
    run_text("CFG", "0123456789ABCDEF");
    CHECK(f_is_err(2, "EARG")); // 16 chars: too long
    run0("CFG", 1);
    CHECK(f_is_err(3, "EARG"));
    cmd_t c;
    cmd_begin(&c, 0, 1, "CFG", 6);
    cbor_encode_text_stringz(&c.arr, "BATCH");
    cbor_encode_int(&c.arr, 1);
    cbor_encode_int(&c.arr, 2);
    cmd_send(&c);
    CHECK(f_is_err(4, "EARG"));
}

/* ---- argument shapes, for every verb that takes a name ------------------ */

TEST(name_verbs_reject_missing_wrong_type_long_and_extra_arguments)
{
    reset();
    channel_registry_register("a", "uint", "RO", NULL, get_const, NULL);
    const char *verbs[] = { "ECHO", "GET", "SET", "SUB", "UNSUB" };
    char longname[200]; // longer than ECHO's 128 and any 40-character name
    memset(longname, 'q', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        g_frame_count = 0;
        run0(verbs[i], 1); // missing
        {
            cmd_t c;
            cmd_begin(&c, 0, 1, verbs[i], 4);
            cbor_encode_int(&c.arr, 5);
            cmd_send(&c);
        } // not text
        run_text(verbs[i], longname); // too long
        {
            cmd_t c;
            cmd_begin(&c, 0, 1, verbs[i], 6); // an extra argument
            cbor_encode_text_stringz(&c.arr, "a");
            cbor_encode_int(&c.arr, 1);
            cbor_encode_int(&c.arr, 2);
            cmd_send(&c);
        }
        CHECK_EQ(g_frame_count, 4);
        for (int f = 0; f < g_frame_count; f++) {
            CHECK(f_is_err(f, "EARG"));
        }
    }
}

TEST(cfg_text_value_must_be_text_and_fit)
{
    reset();
    run_text_int("CFG", "ENCODING", 5);
    CHECK(f_is_err(0, "EARG"));
    run_text_text("CFG", "ENCODING", "0123456789ABCDEF");
    CHECK(f_is_err(1, "EARG")); // 16 chars
    run_text_int("CFG", "LED_MODE", 5);
    CHECK(f_is_err(2, "EARG"));
}

TEST(a_frame_cut_short_inside_the_array_is_counted)
{
    reset();
    uint8_t cut_after_kind[] = { 0x83, 0x00 }; // declares 3 items, holds 1
    proto_cmd_dispatch(cut_after_kind, sizeof(cut_after_kind));
    uint8_t cut_after_seq[] = { 0x83, 0x00, 0x01 };
    proto_cmd_dispatch(cut_after_seq, sizeof(cut_after_seq));
    CHECK_EQ(g_frame_count + g_crc_errors >= 1, 1);
}

/* ---- SUB / UNSUB / START / STOP / SAVE / LOAD ------------------------- */

TEST(subscription_and_streaming_commands)
{
    reset();
    channel_registry_register("a", "uint", "RO", NULL, get_const, NULL);
    run0("START", 1);
    CHECK(f_is_err(0, "ESTATE")); // nothing subscribed
    run_text("SUB", "a");
    CHECK(f_is_ok(1));
    run_text("SUB", "zzz");
    CHECK(f_is_err(2, "ENOCH"));
    run0("START", 3);
    CHECK(f_is_ok(3));
    run0("START", 4);
    CHECK(f_is_err(4, "ESTATE"));
    run0("STOP", 5);
    CHECK(f_is_ok(5));
    run0("STOP", 6);
    CHECK(f_is_err(6, "ESTATE"));
    run_text("UNSUB", "a");
    CHECK(f_is_ok(7));
    run_text("UNSUB", "a");
    CHECK(f_is_err(8, "ESTATE"));
}

TEST(sub_limit_gives_erange)
{
    reset();
    for (int i = 0; i < 9; i++) {
        char n[8];
        snprintf(n, sizeof(n), "c%d", i);
        channel_registry_register(n, "uint", "RO", NULL, get_const, NULL);
    }
    for (int i = 0; i < 8; i++) {
        char n[8];
        snprintf(n, sizeof(n), "c%d", i);
        run_text("SUB", n);
        CHECK(f_is_ok(i));
    }
    run_text("SUB", "c8");
    CHECK(f_is_err(8, "ERANGE"));
}

TEST(save_and_load_commands)
{
    reset();
    run0("LOAD", 1);
    CHECK(f_is_err(0, "ESTATE")); // nothing saved
    run0("SAVE", 2);
    CHECK(f_is_ok(1));
    run0("LOAD", 3);
    CHECK(f_is_ok(2));
    g_nvs_fail_open = 1;
    run0("SAVE", 4);
    CHECK(f_is_err(3, "EINTERNAL"));
    run0("LOAD", 5);
    CHECK(f_is_err(4, "EINTERNAL"));
    g_nvs_fail_open = 0;
}

/* ---- verb registration --------------------------------------------------- */

static void custom_handler(uint32_t seq, CborValue *it)
{
    (void)it;
    proto_reply_ok0(seq, "CUSTOM");
}

TEST(register_verb_rules)
{
    reset();
    CHECK_EQ(proto_cmd_register_verb("CUSTOM", custom_handler), ESP_OK);
    run0("CUSTOM", 1);
    CHECK(f_is_ok(0));
    CHECK_EQ(proto_cmd_register_verb("CUSTOM", custom_handler), ESP_ERR_INVALID_STATE);
    CHECK_EQ(proto_cmd_register_verb("PING", custom_handler), ESP_ERR_INVALID_STATE);
}

TEST(register_verb_rejects_names_that_would_be_cut_short)
{
    // The stored name holds 11 characters. A longer one would be cut, then
    // never match what a host sends, and the duplicate check would miss it.
    reset();
    CHECK_EQ(proto_cmd_register_verb("ABCDEFGHIJK", custom_handler), ESP_OK);               // 11: fits
    CHECK_EQ(proto_cmd_register_verb("ABCDEFGHIJKL", custom_handler), ESP_ERR_INVALID_ARG); // 12
    CHECK_EQ(proto_cmd_register_verb("", custom_handler), ESP_ERR_INVALID_ARG);
    CHECK_EQ(proto_cmd_register_verb(NULL, custom_handler), ESP_ERR_INVALID_ARG);
    CHECK_EQ(proto_cmd_register_verb("OKNAME", NULL), ESP_ERR_INVALID_ARG);
}

TEST(verb_table_full_at_24)
{
    reset(); // 15 built-ins
    char n[8];
    for (int i = 0; i < 9; i++) {
        snprintf(n, sizeof(n), "V%d", i);
        CHECK_EQ(proto_cmd_register_verb(n, custom_handler), ESP_OK);
    }
    CHECK_EQ(proto_cmd_register_verb("ONEMORE", custom_handler), ESP_ERR_NO_MEM);
    CHECK_EQ(s_command_count, (size_t)MAX_VERBS);
}

TEST(init_twice_does_not_duplicate_verbs)
{
    reset();
    size_t n = s_command_count;
    proto_cmd_init("Again");
    CHECK_EQ(s_command_count, n);
    CHECK_EQ(n, 15u);
}

int main(void)
{
    RUN_TEST(reply_shapes);
    RUN_TEST(oversize_reply_becomes_an_error_not_a_broken_frame);
    RUN_TEST(a_reply_that_exactly_fits_is_sent_whole);
    RUN_TEST(ping_id_stat_basic);
    RUN_TEST(extra_arguments_are_rejected);
    RUN_TEST(rst_replies_then_restarts);
    RUN_TEST(unknown_verb_and_wrong_kind);
    RUN_TEST(verb_of_15_chars_is_unknown_16_is_a_frame_error);
    RUN_TEST(malformed_frames_are_counted_not_answered);
    RUN_TEST(sequence_number_out_of_range_is_not_trusted);
    RUN_TEST(echo_127_ok_128_rejected);
    RUN_TEST(get_single_channel_and_missing);
    RUN_TEST(a_get_callback_that_encodes_nothing_gives_an_error_frame);
    RUN_TEST(get_name_length_limit);
    RUN_TEST(get_all_sends_rows_then_count_and_runs_the_hook);
    RUN_TEST(get_all_with_empty_registry);
    RUN_TEST(set_paths);
    RUN_TEST(caps_lists_every_channel);
    RUN_TEST(cfg_period_us);
    RUN_TEST(cfg_batch);
    RUN_TEST(cfg_text_keys);
    RUN_TEST(cfg_unknown_or_malformed);
    RUN_TEST(name_verbs_reject_missing_wrong_type_long_and_extra_arguments);
    RUN_TEST(cfg_text_value_must_be_text_and_fit);
    RUN_TEST(a_frame_cut_short_inside_the_array_is_counted);
    RUN_TEST(subscription_and_streaming_commands);
    RUN_TEST(sub_limit_gives_erange);
    RUN_TEST(save_and_load_commands);
    RUN_TEST(register_verb_rules);
    RUN_TEST(register_verb_rejects_names_that_would_be_cut_short);
    RUN_TEST(verb_table_full_at_24);
    RUN_TEST(init_twice_does_not_duplicate_verbs);
    return tinytest_summary();
}
