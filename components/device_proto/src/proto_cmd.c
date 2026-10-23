#include "proto_cmd.h"
#include "proto_reply.h"
#include "protocol.h"
#include "proto_util.h"
#include "err_codes.h"
#include "channel_registry.h"
#include "device_config.h"
#include "device_stream.h"
#include "usb_cdc.h"
#include "cbor.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static char s_device_name[48] = "ESP32-S3-Sensor-Device";
static void (*s_on_get_hook)(void) = NULL;

void proto_cmd_set_on_get_hook(void (*hook)(void))
{
    s_on_get_hook = hook;
}

// ---------------------------------------------------------------------------
// System commands
// ---------------------------------------------------------------------------

static void handle_ping(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "PING", "EARG");
        return;
    }
    proto_reply_ok0(seq, "PING");
}

static void handle_id(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "ID", "EARG");
        return;
    }
    const esp_app_desc_t *desc = esp_app_get_description();
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, "ID", 6);
    cbor_encode_text_stringz(&arr, s_device_name);
    cbor_encode_text_stringz(&arr, desc->version);
    cbor_encode_text_stringz(&arr, usb_cdc_get_serial());
    proto_reply_send(&enc, &arr);
}

static void handle_caps(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "CAPS", "EARG");
        return;
    }
    size_t count = channel_registry_count();
    for (size_t i = 0; i < count; i++) {
        const channel_entry_t *ch = channel_registry_at(i);
        CborEncoder enc, arr;
        proto_reply_begin(&enc, &arr, FRAME_KIND_ROW, seq, "CAPS", 6);
        cbor_encode_text_stringz(&arr, ch->name);
        cbor_encode_text_stringz(&arr, ch->type_name);
        cbor_encode_text_stringz(&arr, ch->access);
        proto_reply_send(&enc, &arr);
    }
    proto_reply_ok_uint(seq, "CAPS", count);
}

static void handle_stat(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "STAT", "EARG");
        return;
    }
    usb_cdc_stats_t s;
    usb_cdc_get_stats(&s);
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, "STAT", 9);
    cbor_encode_uint(&arr, (uint64_t)esp_timer_get_time());
    cbor_encode_uint(&arr, s.frames_rx);
    cbor_encode_uint(&arr, s.frames_tx);
    cbor_encode_uint(&arr, s.crc_errors);
    cbor_encode_uint(&arr, s.samples_dropped);
    cbor_encode_uint(&arr, s.log_lines_dropped);
    proto_reply_send(&enc, &arr);
}

static void handle_rst(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "RST", "EARG");
        return;
    }
    proto_reply_ok0(seq, "RST");
    // Give CDC0 a moment to actually flush this response before the reset
    // tears the USB link down.
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void handle_echo(uint32_t seq, CborValue *it)
{
    if (cbor_value_at_end(it) || !cbor_value_is_text_string(it)) {
        proto_reply_err(seq, "ECHO", "EARG");
        return;
    }
    char text[128];
    CborValue after;
    if (!proto_copy_cbor_text(it, text, sizeof(text), &after)) {
        proto_reply_err(seq, "ECHO", "EARG");
        return;
    }
    if (!cbor_value_at_end(&after)) {
        proto_reply_err(seq, "ECHO", "EARG");
        return;
    }
    proto_reply_ok_text(seq, "ECHO", text);
}

// ---------------------------------------------------------------------------
// Data commands
// ---------------------------------------------------------------------------

static void handle_get(uint32_t seq, CborValue *it)
{
    if (cbor_value_at_end(it) || !cbor_value_is_text_string(it)) {
        proto_reply_err(seq, "GET", "EARG");
        return;
    }
    char name[PROTO_NAME_MAXLEN];
    CborValue after;
    if (!proto_copy_cbor_text(it, name, sizeof(name), &after)) {
        proto_reply_err(seq, "GET", "EARG");
        return;
    }
    if (!cbor_value_at_end(&after)) {
        proto_reply_err(seq, "GET", "EARG");
        return;
    }

    if (s_on_get_hook != NULL) {
        s_on_get_hook();
    }

    if (strcmp(name, "*") == 0) {
        size_t count = channel_registry_count();
        for (size_t i = 0; i < count; i++) {
            const channel_entry_t *ch = channel_registry_at(i);
            CborEncoder enc, arr;
            proto_reply_begin(&enc, &arr, FRAME_KIND_ROW, seq, "GET", 5);
            cbor_encode_text_stringz(&arr, ch->name);
            ch->get(ch->ctx, &arr);
            proto_reply_send(&enc, &arr);
        }
        proto_reply_ok_uint(seq, "GET", count);
        return;
    }

    const channel_entry_t *ch = channel_registry_find(name);
    if (ch == NULL) {
        proto_reply_err(seq, "GET", "ENOCH");
        return;
    }
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_OK, seq, "GET", 4);
    ch->get(ch->ctx, &arr);
    proto_reply_send(&enc, &arr);
}

static void handle_set(uint32_t seq, CborValue *it)
{
    if (cbor_value_at_end(it) || !cbor_value_is_text_string(it)) {
        proto_reply_err(seq, "SET", "EARG");
        return;
    }
    char name[PROTO_NAME_MAXLEN];
    CborValue val;
    if (!proto_copy_cbor_text(it, name, sizeof(name), &val)) {
        proto_reply_err(seq, "SET", "EARG");
        return;
    }
    if (cbor_value_at_end(&val)) {
        proto_reply_err(seq, "SET", "EARG");
        return;
    }
    CborValue after = val;
    if (cbor_value_advance(&after) != CborNoError || !cbor_value_at_end(&after)) {
        proto_reply_err(seq, "SET", "EARG");
        return;
    }

    const channel_entry_t *ch = channel_registry_find(name);
    if (ch == NULL) {
        proto_reply_err(seq, "SET", "ENOCH");
        return;
    }
    if (ch->set == NULL) {
        proto_reply_err(seq, "SET", "EACCESS");
        return;
    }

    int rc = ch->set(ch->ctx, &val);
    if (rc == ERR_RANGE) {
        proto_reply_err(seq, "SET", "ERANGE");
        return;
    }
    if (rc != 0) {
        proto_reply_err(seq, "SET", "EINTERNAL");
        return;
    }
    proto_reply_ok0(seq, "SET");
}

// ---------------------------------------------------------------------------
// Streaming commands
// ---------------------------------------------------------------------------

// 1 hour. A mistaken huge value (an extra zero typed into a host script)
// should be rejected clearly, rather than making streaming look broken for
// a very long time with no error to explain why.
#define PERIOD_US_MAX 3600000000ULL

// Reads a CFG integer argument in 0..max. False for a non-integer, an integer
// outside 64 bits, or a value outside the range.
static bool read_uint_arg(const CborValue *val, uint64_t max, uint64_t *out)
{
    int64_t v;
    if (!cbor_value_is_integer(val) || cbor_value_get_int64_checked(val, &v) != CborNoError || v < 0 ||
        (uint64_t)v > max) {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

// Reads a CFG text argument that fits in size bytes with its terminator.
static bool read_text_arg(CborValue *val, char *buf, size_t size)
{
    CborValue next;
    return cbor_value_is_text_string(val) && proto_copy_cbor_text(val, buf, size, &next);
}

// Each CFG key has one function. has_value is false for a read (CFG <key>).
typedef void (*cfg_key_fn)(uint32_t seq, CborValue *val, bool has_value);

// cppcheck-suppress constParameterCallback ; every cfg_key_fn shares one signature
static void cfg_period_us(uint32_t seq, CborValue *val, bool has_value)
{
    if (has_value) {
        uint64_t v;
        if (!read_uint_arg(val, PERIOD_US_MAX, &v)) {
            proto_reply_err(seq, "CFG", "EARG");
            return;
        }
        if (device_config_set_period_us((uint32_t)v) == ERR_RANGE) {
            proto_reply_err(seq, "CFG", "ERANGE");
            return;
        }
        device_stream_apply_period();
    }
    proto_reply_ok_uint(seq, "CFG", device_config_get_period_us());
}

// cppcheck-suppress constParameterCallback ; every cfg_key_fn shares one signature
static void cfg_batch(uint32_t seq, CborValue *val, bool has_value)
{
    if (has_value) {
        uint64_t v;
        if (!read_uint_arg(val, 0xFFFF, &v)) {
            proto_reply_err(seq, "CFG", "EARG");
            return;
        }
        if (device_config_set_batch((uint16_t)v) == ERR_RANGE) {
            proto_reply_err(seq, "CFG", "ERANGE");
            return;
        }
    }
    proto_reply_ok_uint(seq, "CFG", device_config_get_batch());
}

// MODE and LED_MODE share one shape: a text value, checked by a setter.
static void cfg_text_key(uint32_t seq, CborValue *val, bool has_value, size_t size, int (*set)(const char *),
                         const char *(*get)(void))
{
    if (has_value) {
        char text[16];
        if (size > sizeof(text) || !read_text_arg(val, text, size)) {
            proto_reply_err(seq, "CFG", "EARG");
            return;
        }
        if (set(text) == ERR_RANGE) {
            proto_reply_err(seq, "CFG", "ERANGE");
            return;
        }
    }
    proto_reply_ok_text(seq, "CFG", get());
}

static void cfg_mode(uint32_t seq, CborValue *val, bool has_value)
{
    cfg_text_key(seq, val, has_value, 16, device_config_set_mode, device_config_get_mode);
}

static void cfg_led_mode(uint32_t seq, CborValue *val, bool has_value)
{
    cfg_text_key(seq, val, has_value, 8, device_config_set_led_mode, device_config_get_led_mode);
}

static void cfg_encoding(uint32_t seq, CborValue *val, bool has_value)
{
    if (has_value) {
        char text[16];
        if (!read_text_arg(val, text, sizeof(text))) {
            proto_reply_err(seq, "CFG", "EARG");
            return;
        }
        if (strcmp(text, "CBOR") != 0) {
            proto_reply_err(seq, "CFG", "ERANGE");
            return;
        }
    }
    proto_reply_ok_text(seq, "CFG", device_config_get_encoding());
}

static const struct {
    const char *key;
    cfg_key_fn fn;
} s_cfg_keys[] = {
    { "PERIOD_US", cfg_period_us }, { "BATCH", cfg_batch },       { "MODE", cfg_mode },
    { "ENCODING", cfg_encoding },   { "LED_MODE", cfg_led_mode },
};

static void handle_cfg(uint32_t seq, CborValue *it)
{
    char key[16];
    bool has_value;
    CborValue val;
    if (!proto_parse_key_value_args(it, key, sizeof(key), &has_value, &val)) {
        proto_reply_err(seq, "CFG", "EARG");
        return;
    }
    for (size_t i = 0; i < sizeof(s_cfg_keys) / sizeof(s_cfg_keys[0]); i++) {
        if (strcmp(key, s_cfg_keys[i].key) == 0) {
            s_cfg_keys[i].fn(seq, &val, has_value);
            return;
        }
    }
    proto_reply_err(seq, "CFG", "EARG");
}

static void handle_sub(uint32_t seq, CborValue *it)
{
    if (cbor_value_at_end(it) || !cbor_value_is_text_string(it)) {
        proto_reply_err(seq, "SUB", "EARG");
        return;
    }
    char name[PROTO_NAME_MAXLEN];
    CborValue after;
    if (!proto_copy_cbor_text(it, name, sizeof(name), &after)) {
        proto_reply_err(seq, "SUB", "EARG");
        return;
    }
    if (!cbor_value_at_end(&after)) {
        proto_reply_err(seq, "SUB", "EARG");
        return;
    }
    int rc = device_config_subscribe(name);
    if (rc == ERR_NOCH) {
        proto_reply_err(seq, "SUB", "ENOCH");
        return;
    }
    if (rc == ERR_RANGE) {
        proto_reply_err(seq, "SUB", "ERANGE");
        return;
    }
    proto_reply_ok0(seq, "SUB");
}

static void handle_unsub(uint32_t seq, CborValue *it)
{
    if (cbor_value_at_end(it) || !cbor_value_is_text_string(it)) {
        proto_reply_err(seq, "UNSUB", "EARG");
        return;
    }
    char name[PROTO_NAME_MAXLEN];
    CborValue after;
    if (!proto_copy_cbor_text(it, name, sizeof(name), &after)) {
        proto_reply_err(seq, "UNSUB", "EARG");
        return;
    }
    if (!cbor_value_at_end(&after)) {
        proto_reply_err(seq, "UNSUB", "EARG");
        return;
    }
    if (device_config_unsubscribe(name) == ERR_STATE) {
        proto_reply_err(seq, "UNSUB", "ESTATE");
        return;
    }
    proto_reply_ok0(seq, "UNSUB");
}

static void handle_start(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "START", "EARG");
        return;
    }
    if (device_config_start_streaming() == ERR_STATE) {
        proto_reply_err(seq, "START", "ESTATE");
        return;
    }
    proto_reply_ok0(seq, "START");
}

static void handle_stop(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "STOP", "EARG");
        return;
    }
    if (device_config_stop_streaming() == ERR_STATE) {
        proto_reply_err(seq, "STOP", "ESTATE");
        return;
    }
    proto_reply_ok0(seq, "STOP");
}

// ---------------------------------------------------------------------------
// Storage commands
// ---------------------------------------------------------------------------

static void handle_save(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "SAVE", "EARG");
        return;
    }
    if (device_config_save() != ESP_OK) {
        proto_reply_err(seq, "SAVE", "EINTERNAL");
        return;
    }
    proto_reply_ok0(seq, "SAVE");
}

static void handle_load(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "LOAD", "EARG");
        return;
    }
    esp_err_t err = device_config_load();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        proto_reply_err(seq, "LOAD", "ESTATE");
        return;
    }
    if (err != ESP_OK) {
        proto_reply_err(seq, "LOAD", "EINTERNAL");
        return;
    }
    proto_reply_ok0(seq, "LOAD");
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

typedef struct {
    char verb[12];
    proto_cmd_handler_t handler;
} cmd_entry_t;

// 15 built-in verbs, plus room for SENS, SCAPS, SCFG, and CAL (see
// sensor_core.c) and a little headroom for a future device to add its own.
#define MAX_VERBS 24

static cmd_entry_t s_commands[MAX_VERBS];
static size_t s_command_count = 0;

static void add_builtin(const char *verb, proto_cmd_handler_t handler)
{
    // A driver must not trust its caller, even its own init function: this
    // bound was previously checked only in the public
    // proto_cmd_register_verb() path, not here. See AGENTS.md, "Firmware
    // mistakes to avoid".
    if (s_command_count >= MAX_VERBS) {
        return;
    }
    strncpy(s_commands[s_command_count].verb, verb, sizeof(s_commands[0].verb) - 1);
    s_commands[s_command_count].handler = handler;
    s_command_count++;
}

void proto_cmd_init(const char *device_name)
{
    if (s_command_count != 0) {
        // Already initialized. A second call must not re-register every
        // built-in verb on top of the first set, which would overflow
        // s_commands and corrupt whatever follows it in .bss. See
        // AGENTS.md, "Firmware mistakes to avoid".
        return;
    }
    if (device_name != NULL) {
        strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
        s_device_name[sizeof(s_device_name) - 1] = '\0';
    }

    add_builtin("PING", handle_ping);
    add_builtin("ID", handle_id);
    add_builtin("CAPS", handle_caps);
    add_builtin("STAT", handle_stat);
    add_builtin("RST", handle_rst);
    add_builtin("ECHO", handle_echo);
    add_builtin("GET", handle_get);
    add_builtin("SET", handle_set);
    add_builtin("CFG", handle_cfg);
    add_builtin("SUB", handle_sub);
    add_builtin("UNSUB", handle_unsub);
    add_builtin("START", handle_start);
    add_builtin("STOP", handle_stop);
    add_builtin("SAVE", handle_save);
    add_builtin("LOAD", handle_load);
}

esp_err_t proto_cmd_register_verb(const char *verb, proto_cmd_handler_t handler)
{
    // The stored name holds sizeof(verb) - 1 characters. A longer one would
    // be cut, then never match what a host sends, and the duplicate check
    // below would miss it. Reject it instead.
    if (verb == NULL || handler == NULL || verb[0] == '\0' || strlen(verb) >= sizeof(s_commands[0].verb)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_command_count; i++) {
        if (strcmp(s_commands[i].verb, verb) == 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (s_command_count >= MAX_VERBS) {
        return ESP_ERR_NO_MEM;
    }
    add_builtin(verb, handler);
    return ESP_OK;
}

typedef enum {
    HDR_OK,
    HDR_UNREADABLE, // no trustworthy sequence number: count it, do not answer
    HDR_BAD_VERB,   // a sequence number was found, but the verb is missing or too long
} hdr_status_t;

typedef struct {
    CborParser parser;
    CborValue args; // positioned at the first argument after the verb
    uint32_t seq;
    int64_t kind;
    char verb[16];
} cmd_header_t;

// Reads one integer item and steps past it.
static bool read_int_item(CborValue *it, int64_t *out)
{
    return cbor_value_is_integer(it) && cbor_value_get_int64(it, out) == CborNoError &&
           cbor_value_advance(it) == CborNoError;
}

// Reads [kind, seq, verb, ...]. docs/05-message-model.md: the sequence number
// is 0..65535. A value outside that range cannot be echoed back truthfully, so
// it is as unreadable as a broken array.
static hdr_status_t parse_command_header(const uint8_t *payload, size_t len, cmd_header_t *h)
{
    CborValue top, it;
    size_t arr_len = 0;
    int64_t seq = 0;

    if (cbor_parser_init(payload, len, 0, &h->parser, &top) != CborNoError || !cbor_value_is_array(&top) ||
        cbor_value_get_array_length(&top, &arr_len) != CborNoError || arr_len < 3 ||
        cbor_value_enter_container(&top, &it) != CborNoError || !read_int_item(&it, &h->kind) ||
        !read_int_item(&it, &seq) || seq < 0 || seq > 65535) {
        return HDR_UNREADABLE;
    }
    h->seq = (uint32_t)seq;
    // A trustworthy sequence number has been found. Every problem from here
    // on gets a matching ERR response. See docs/11-error-codes.md.
    if (!cbor_value_is_text_string(&it) || !proto_copy_cbor_text(&it, h->verb, sizeof(h->verb), &it)) {
        return HDR_BAD_VERB;
    }
    h->args = it;
    return HDR_OK;
}

void proto_cmd_dispatch(const uint8_t *payload, size_t len)
{
    cmd_header_t h;
    switch (parse_command_header(payload, len, &h)) {
    case HDR_UNREADABLE:
        usb_cdc_note_crc_error();
        return;
    case HDR_BAD_VERB:
        proto_reply_err(h.seq, "", "EFRAME");
        return;
    case HDR_OK:
        break;
    }

    if (h.kind != FRAME_KIND_CMD) {
        proto_reply_err(h.seq, h.verb, "EFRAME");
        return;
    }
    for (size_t i = 0; i < s_command_count; i++) {
        if (strcmp(h.verb, s_commands[i].verb) == 0) {
            s_commands[i].handler(h.seq, &h.args);
            return;
        }
    }
    proto_reply_err(h.seq, h.verb, "ECMD");
}
