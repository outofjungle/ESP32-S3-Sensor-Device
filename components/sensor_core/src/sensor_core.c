#include "sensor_core.h"
#include "channel_registry.h"
#include "device_config.h"
#include "proto_cmd.h"
#include "proto_reply.h"
#include "proto_util.h"
#include "input_validate.h"
#include "sensor_cfg_logic.h"
#include "sensor_inst_state.h"
#include "err_codes.h"
#include "protocol.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sensor_core";

#define MAX_SENSOR_INSTANCES     4
#define MAX_SENSOR_CHANNEL_SHIMS 32
#define MAX_SENSOR_CFGKEYS       (MAX_SENSOR_INSTANCES * SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER)
#define NVS_NAMESPACE            "sensor_cfg"
#define SENSOR_CFG_BLOB_VERSION  1
#define SCFG_ENUM_LIST_MAXLEN    128

struct sensor_inst {
    char name[16]; // also the NVS key for this instance's config blob --
                   // NVS keys are at most 15 bytes, so instance names must
                   // stay within that. See docs/16-adding-a-device.md.
    const sensor_driver_t *drv;
    sensor_board_binding_t binding;
    void *user_ctx;
    sensor_inst_state_t state;
    portMUX_TYPE state_mux; // guards `state` -- two FreeRTOS tasks can run
                            // at the same instant on this chip's two CPU
                            // cores. See AGENTS.md, "Firmware mistakes to
                            // avoid".
    int64_t next_poll_ms;
};

static sensor_inst_state_t get_inst_state(sensor_inst_t *inst)
{
    portENTER_CRITICAL(&inst->state_mux);
    sensor_inst_state_t s = inst->state;
    portEXIT_CRITICAL(&inst->state_mux);
    return s;
}

static void set_inst_state(sensor_inst_t *inst, sensor_inst_state_t state)
{
    portENTER_CRITICAL(&inst->state_mux);
    inst->state = state;
    portEXIT_CRITICAL(&inst->state_mux);
}

#define state_is_operational scl_state_is_operational

static sensor_inst_t s_instances[MAX_SENSOR_INSTANCES];
static size_t s_instance_count = 0;

typedef struct {
    sensor_inst_t *inst;
    const sensor_channel_def_t *def;
} channel_shim_t;

static channel_shim_t s_channel_shims[MAX_SENSOR_CHANNEL_SHIMS];
static size_t s_channel_shim_count = 0;

typedef struct {
    char full_name[PROTO_NAME_MAXLEN];
    sensor_inst_t *inst;
    const sensor_driver_t *drv;
    const sensor_cfgkey_def_t *def;
} cfgkey_entry_t;

static cfgkey_entry_t s_cfgkeys[MAX_SENSOR_CFGKEYS];
static size_t s_cfgkey_count = 0;

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

void sensor_core_set_user_ctx(sensor_inst_t *inst, void *ctx)
{
    inst->user_ctx = ctx;
}

// cppcheck-suppress constParameterPointer ; the public handle type is not const-qualified
void *sensor_core_get_user_ctx(sensor_inst_t *inst)
{
    return inst->user_ctx;
}

const sensor_board_binding_t *sensor_core_get_binding(sensor_inst_t *inst)
{
    return &inst->binding;
}

// cppcheck-suppress constParameterPointer ; same reason as sensor_core_get_user_ctx
const char *sensor_core_get_name(sensor_inst_t *inst)
{
    return inst->name;
}

bool sensor_core_any_fault(void)
{
    for (size_t i = 0; i < s_instance_count; i++) {
        if (!state_is_operational(get_inst_state(&s_instances[i]))) {
            return true;
        }
    }
    return false;
}

void sensor_core_set_fault(sensor_inst_t *inst)
{
    // No further poll() calls, and SCFG/GET on this instance's channels
    // and keys report ESTATE/null, until a restart. This is the same
    // recovery model an initial probe() failure already uses -- see
    // docs/09-sensor-configuration.md, "SENS".
    set_inst_state(inst, SENSOR_INST_FAULT);
}

static sensor_inst_t *find_instance_by_name(const char *name)
{
    for (size_t i = 0; i < s_instance_count; i++) {
        if (strcmp(s_instances[i].name, name) == 0) {
            return &s_instances[i];
        }
    }
    return NULL;
}

static cfgkey_entry_t *find_cfgkey(const char *full_name)
{
    for (size_t i = 0; i < s_cfgkey_count; i++) {
        if (strcmp(s_cfgkeys[i].full_name, full_name) == 0) {
            return &s_cfgkeys[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Channel registry shim: lets channel_registry.h stay sensor-agnostic (ctx
// is "void *") while a driver's own callbacks take sensor_inst_t *.
// ---------------------------------------------------------------------------

static esp_err_t channel_shim_get(void *ctx, CborEncoder *enc)
{
    channel_shim_t *s = (channel_shim_t *)ctx;
    if (!state_is_operational(get_inst_state(s->inst))) {
        // The instance has not finished probing, a probe/init/start step
        // failed, or the driver has since reported it can no longer
        // trust its own data (sensor_core_set_fault()). A channel's
        // get() callback may read a per-instance context a driver has
        // not set up yet -- never call into the driver in that case. A
        // host can GET a channel the moment it appears on the wire,
        // which is before probe() runs. RUNNING and BUSY both call
        // through: a channel read is a cached-value read, never I2C, so
        // it is safe even while a CAL action is using the bus. See
        // AGENTS.md, "Firmware mistakes to avoid".
        return sensor_cbor_to_esp(cbor_encode_null(enc));
    }
    return s->def->get(s->inst, enc);
}

static int channel_shim_set(void *ctx, CborValue *val)
{
    channel_shim_t *s = (channel_shim_t *)ctx;
    if (!state_is_operational(get_inst_state(s->inst))) {
        return ERR_STATE; // see channel_shim_get's comment, above
    }
    return s->def->set(s->inst, val);
}

// ---------------------------------------------------------------------------
// Per-instance NVS persistence. See docs/09-sensor-configuration.md.
// ---------------------------------------------------------------------------

static void load_instance_cfg(sensor_inst_t *inst)
{
    const sensor_driver_t *drv = inst->drv;
    uint8_t buf[1 + 4 * SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER];
    size_t got_len = 0;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        got_len = sizeof(buf);
        if (nvs_get_blob(h, inst->name, buf, &got_len) != ESP_OK) {
            got_len = 0;
        }
        nvs_close(h);
    }

    // A value loaded from flash is as untrusted as one from the wire: flash
    // can bit-rot, a commit can be interrupted, and a later build can move a
    // key without the version byte changing. scl_blob_unpack() range-checks
    // each value and substitutes the key's default for a bad one. See
    // AGENTS.md, "Firmware mistakes to avoid".
    int32_t values[SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER];
    size_t replaced = 0;
    bool have_saved =
        scl_blob_unpack(buf, got_len, SENSOR_CFG_BLOB_VERSION, drv->cfgkeys, drv->cfgkey_count, values, &replaced);
    if (replaced > 0) {
        ESP_LOGW(TAG, "%s: %u saved value(s) out of range, defaults used for them", inst->name, (unsigned)replaced);
    }
    // Two passes: a key that depends on another (a low threshold that
    // must not exceed a high one) can be refused only because the other
    // key still holds its old value. A key refused with ESP_ERR_INVALID_ARG
    // is tried once more after every other key has been applied.
    bool retry[SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER] = { false };
    for (size_t i = 0; i < drv->cfgkey_count; i++) {
        esp_err_t err = drv->cfg_set(inst, &drv->cfgkeys[i], values[i]);
        if (err == ESP_ERR_INVALID_ARG) {
            retry[i] = true;
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s.%s: applying %s value %ld failed: %d", inst->name, drv->cfgkeys[i].name,
                     have_saved ? "saved" : "default", (long)values[i], err);
        }
    }
    for (size_t i = 0; i < drv->cfgkey_count; i++) {
        if (!retry[i]) {
            continue;
        }
        esp_err_t err = drv->cfg_set(inst, &drv->cfgkeys[i], values[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s.%s: applying %s value %ld failed: %d", inst->name, drv->cfgkeys[i].name,
                     have_saved ? "saved" : "default", (long)values[i], err);
        }
    }
}

static esp_err_t save_instance_cfg(sensor_inst_t *inst)
{
    const sensor_driver_t *drv = inst->drv;
    uint8_t buf[1 + 4 * SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER];
    int32_t values[SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER] = { 0 };

    for (size_t i = 0; i < drv->cfgkey_count; i++) {
        int32_t v = drv->cfgkeys[i].default_value;
        drv->cfg_get(inst, &drv->cfgkeys[i], &v); // best-effort; keep default on failure
        values[i] = v;
    }
    size_t blob_len = scl_blob_pack(values, drv->cfgkey_count, SENSOR_CFG_BLOB_VERSION, buf);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, inst->name, buf, blob_len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// The combined persistence hook registered with device_config, so SAVE and
// LOAD cover sensor configuration alongside device-level configuration. See
// docs/09-sensor-configuration.md.
static esp_err_t sensor_core_save_all(void)
{
    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < s_instance_count; i++) {
        if (!state_is_operational(get_inst_state(&s_instances[i]))) {
            continue; // can't read back from a chip that isn't there
        }
        esp_err_t err = save_instance_cfg(&s_instances[i]);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

static esp_err_t sensor_core_load_all(void)
{
    for (size_t i = 0; i < s_instance_count; i++) {
        if (!state_is_operational(get_inst_state(&s_instances[i]))) {
            continue;
        }
        load_instance_cfg(&s_instances[i]);
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// The poll task. See docs/15-adding-a-sensor.md, "A channel's get() must
// not block": this is the only task that ever calls a driver's poll().
// ---------------------------------------------------------------------------

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        for (size_t i = 0; i < s_instance_count; i++) {
            sensor_inst_t *inst = &s_instances[i];
            if (!scl_state_can_poll(get_inst_state(inst))) {
                // Not yet running, faulted, or BUSY with a CAL action --
                // skip this instance's poll cycle rather than race the
                // action's own I2C sequence. See handle_cal().
                continue;
            }
            if (now_ms >= inst->next_poll_ms) {
                esp_err_t err = inst->drv->poll(inst);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "%s: poll failed: %d", inst->name, err);
                }
                inst->next_poll_ms = now_ms + inst->drv->poll_interval_ms(inst);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---------------------------------------------------------------------------
// SCFG/SCAPS value encoding and parsing
// ---------------------------------------------------------------------------

static void encode_cfgkey_row(uint32_t seq, const cfgkey_entry_t *e)
{
    const sensor_cfgkey_def_t *def = e->def;
    CborEncoder enc, arr;
    proto_reply_begin(&enc, &arr, FRAME_KIND_ROW, seq, "SCAPS", 10);
    cbor_encode_text_stringz(&arr, e->full_name);

    const char *type_text;
    switch (def->type) {
    case SENSOR_VAL_INT:
        type_text = "int";
        break;
    case SENSOR_VAL_BOOL:
        type_text = "bool";
        break;
    case SENSOR_VAL_ENUM:
        type_text = "enum";
        break;
    case SENSOR_VAL_UINT:
    default:
        type_text = "uint";
        break;
    }
    cbor_encode_text_stringz(&arr, type_text);

    if (def->type == SENSOR_VAL_ENUM) {
        cbor_encode_int(&arr, 0);
        cbor_encode_int(&arr, (int64_t)def->enum_count - 1);
        cbor_encode_text_stringz(&arr, scl_enum_text(def, def->default_value));
    } else if (def->type == SENSOR_VAL_BOOL) {
        cbor_encode_int(&arr, 0);
        cbor_encode_int(&arr, 1);
        cbor_encode_boolean(&arr, def->default_value != 0);
    } else {
        cbor_encode_int(&arr, def->min);
        cbor_encode_int(&arr, def->max);
        cbor_encode_int(&arr, def->default_value);
    }

    if (def->unit != NULL) {
        cbor_encode_text_stringz(&arr, def->unit);
    } else {
        cbor_encode_null(&arr);
    }

    if (def->type == SENSOR_VAL_ENUM) {
        char joined[SCFG_ENUM_LIST_MAXLEN];
        scl_join_enum_values(def, joined, sizeof(joined));
        cbor_encode_text_stringz(&arr, joined);
    } else {
        cbor_encode_null(&arr);
    }

    proto_reply_send(&enc, &arr);
}

static void reply_cfg_value(uint32_t seq, const sensor_cfgkey_def_t *def, int32_t raw)
{
    switch (def->type) {
    case SENSOR_VAL_INT:
        proto_reply_ok_int(seq, "SCFG", raw);
        break;
    case SENSOR_VAL_BOOL:
        proto_reply_ok_bool(seq, "SCFG", raw != 0);
        break;
    case SENSOR_VAL_ENUM: {
        // A chip register can hold an index with no matching name (for
        // example a 2-bit field against a 2-entry enum). Never index the
        // name table with it.
        const char *text = scl_enum_text(def, raw);
        if (text == NULL) {
            proto_reply_err(seq, "SCFG", "EINTERNAL");
        } else {
            proto_reply_ok_text(seq, "SCFG", text);
        }
        break;
    }
    case SENSOR_VAL_UINT:
    default:
        proto_reply_ok_uint(seq, "SCFG", (uint64_t)(uint32_t)raw);
        break;
    }
}

// ---------------------------------------------------------------------------
// Verb handlers
// ---------------------------------------------------------------------------

static void handle_sens(uint32_t seq, CborValue *it)
{
    if (!cbor_value_at_end(it)) {
        proto_reply_err(seq, "SENS", "EARG");
        return;
    }
    for (size_t i = 0; i < s_instance_count; i++) {
        sensor_inst_t *inst = &s_instances[i];
        CborEncoder enc, arr;
        proto_reply_begin(&enc, &arr, FRAME_KIND_ROW, seq, "SENS", 8);
        cbor_encode_text_stringz(&arr, inst->name);
        cbor_encode_text_stringz(&arr, inst->drv->type_name);
        cbor_encode_text_stringz(&arr, "i2c");
        cbor_encode_uint(&arr, inst->binding.i2c_addr);
        cbor_encode_text_stringz(&arr, scl_state_text(get_inst_state(inst)));
        proto_reply_send(&enc, &arr);
    }
    proto_reply_ok_uint(seq, "SENS", s_instance_count);
}

static void handle_scaps(uint32_t seq, CborValue *it)
{
    char filter[16];
    bool has_filter = false;
    if (!cbor_value_at_end(it)) {
        if (!cbor_value_is_text_string(it)) {
            proto_reply_err(seq, "SCAPS", "EARG");
            return;
        }
        CborValue after;
        if (!proto_copy_cbor_text(it, filter, sizeof(filter), &after)) {
            proto_reply_err(seq, "SCAPS", "EARG");
            return;
        }
        if (!cbor_value_at_end(&after)) {
            proto_reply_err(seq, "SCAPS", "EARG");
            return;
        }
        has_filter = true;
    }

    size_t count = 0;
    for (size_t i = 0; i < s_cfgkey_count; i++) {
        if (has_filter) {
            if (!scl_scaps_filter_match(s_cfgkeys[i].full_name, filter)) {
                continue;
            }
        }
        encode_cfgkey_row(seq, &s_cfgkeys[i]);
        count++;
    }
    proto_reply_ok_uint(seq, "SCAPS", count);
}

// SCFG <key> <value>: validate, write, read back, compare.
static void scfg_write(uint32_t seq, const cfgkey_entry_t *e, CborValue *val)
{
    int32_t raw;
    if (!scl_parse_cfg_value(val, e->def, &raw)) {
        proto_reply_err(seq, "SCFG", "ERANGE");
        return;
    }
    esp_err_t werr = e->drv->cfg_set(e->inst, e->def, raw);
    if (werr != ESP_OK) {
        proto_reply_err(seq, "SCFG", werr == ESP_ERR_INVALID_ARG ? "ERANGE" : "EINTERNAL");
        return;
    }
    int32_t readback;
    if (e->drv->cfg_get(e->inst, e->def, &readback) != ESP_OK) {
        proto_reply_err(seq, "SCFG", "EINTERNAL");
        return;
    }
    if (readback != raw) {
        proto_reply_err(seq, "SCFG", "EVERIFY");
        return;
    }
    reply_cfg_value(seq, e->def, readback);
}

// SCFG <key>: read live from the chip.
static void scfg_read(uint32_t seq, const cfgkey_entry_t *e)
{
    int32_t cur;
    if (e->drv->cfg_get(e->inst, e->def, &cur) != ESP_OK) {
        proto_reply_err(seq, "SCFG", "EINTERNAL");
        return;
    }
    reply_cfg_value(seq, e->def, cur);
}

static void handle_scfg(uint32_t seq, CborValue *it)
{
    char key[PROTO_NAME_MAXLEN];
    bool has_value;
    CborValue val;
    if (!proto_parse_key_value_args(it, key, sizeof(key), &has_value, &val)) {
        proto_reply_err(seq, "SCFG", "EARG");
        return;
    }
    const cfgkey_entry_t *e = find_cfgkey(key);
    if (e == NULL) {
        proto_reply_err(seq, "SCFG", "ENOCH");
        return;
    }
    // Also rejects BUSY, not only a fault state: a config write during an
    // in-progress CAL action would race that action's own I2C sequence on some
    // drivers, which have no mutex of their own protecting cfg_set() against a
    // concurrent calibration routine. Rejecting it here, once, is simpler and
    // safer than requiring every driver to guard against it itself. See
    // AGENTS.md, "Firmware mistakes to avoid".
    if (!scl_state_can_write_cfg(get_inst_state(e->inst))) {
        proto_reply_err(seq, "SCFG", "ESTATE");
        return;
    }
    if (has_value) {
        scfg_write(seq, e, &val);
    } else {
        scfg_read(seq, e);
    }
}

// CAL <instance>.<action> <target>
static bool parse_cal_args(CborValue *it, char *key, size_t key_size, int64_t *target)
{
    CborValue after_key, after_target;
    if (cbor_value_at_end(it) || !proto_copy_cbor_text(it, key, key_size, &after_key) ||
        !cbor_value_is_integer(&after_key) || cbor_value_get_int64_checked(&after_key, target) != CborNoError) {
        return false;
    }
    after_target = after_key;
    return cbor_value_advance(&after_target) == CborNoError && cbor_value_at_end(&after_target);
}

// Runs one action on an instance that is RUNNING. BUSY for the duration:
// visible on SENS (a host watching the instance mid-calibration sees "busy",
// not "ok"), and the poll task skips this instance entirely while it holds
// this state. A driver whose own I2C sequences can still overlap a stray poll
// cycle that was already in flight the instant this was set (BUSY only
// prevents a *new* cycle from starting) still needs its own real mutex for
// that narrower window -- see sensor_tof.c's cal_mutex. This state is the
// wire-visible, diagnostic layer on top of that, not a replacement for it.
static esp_err_t run_cal(sensor_inst_t *inst, const char *action_name, int32_t target, int32_t *result)
{
    set_inst_state(inst, SENSOR_INST_BUSY);

    // Ranging stopped first, for the drivers that need it, matching the
    // sensor_driver_t::stop contract in sensor_core.h.
    if (inst->drv->stop != NULL) {
        inst->drv->stop(inst);
    }
    esp_err_t err = inst->drv->action(inst, action_name, target, result);
    if (inst->drv->start != NULL) {
        inst->drv->start(inst);
    }

    // Only resume RUNNING if nothing else changed the state while the action
    // ran (for example, the driver itself calling sensor_core_set_fault()
    // partway through) -- do not stomp a fault that happened during
    // calibration back to "ok".
    portENTER_CRITICAL(&inst->state_mux);
    inst->state = scl_cal_finish_state(inst->state);
    portEXIT_CRITICAL(&inst->state_mux);
    return err;
}

static void handle_cal(uint32_t seq, CborValue *it)
{
    char key[PROTO_NAME_MAXLEN];
    int64_t target = 0;
    if (!parse_cal_args(it, key, sizeof(key), &target)) {
        proto_reply_err(seq, "CAL", "EARG");
        return;
    }
    if (target < INT32_MIN || target > INT32_MAX) {
        proto_reply_err(seq, "CAL", "ERANGE"); // would be cut to 32 bits and could pass a driver's range check
        return;
    }
    const char *action_name;
    sensor_inst_t *inst = scl_split_action_key(key, &action_name) ? find_instance_by_name(key) : NULL;
    if (inst == NULL || inst->drv->action == NULL) {
        proto_reply_err(seq, "CAL", "ENOCH");
        return;
    }
    if (!scl_state_can_write_cfg(get_inst_state(inst)) || device_config_is_streaming()) {
        proto_reply_err(seq, "CAL", "ESTATE");
        return;
    }
    int32_t result = 0;
    esp_err_t err = run_cal(inst, action_name, (int32_t)target, &result);
    if (err != ESP_OK) {
        proto_reply_err(seq, "CAL", scl_cal_error_code(err, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND));
        return;
    }
    proto_reply_ok_int(seq, "CAL", result);
}

// ---------------------------------------------------------------------------
// Registration and lifecycle
// ---------------------------------------------------------------------------

esp_err_t sensor_core_init(void)
{
    esp_err_t err;
    if ((err = proto_cmd_register_verb("SENS", handle_sens)) != ESP_OK)
        return err;
    if ((err = proto_cmd_register_verb("SCAPS", handle_scaps)) != ESP_OK)
        return err;
    if ((err = proto_cmd_register_verb("SCFG", handle_scfg)) != ESP_OK)
        return err;
    if ((err = proto_cmd_register_verb("CAL", handle_cal)) != ESP_OK)
        return err;

    device_config_register_persist_hook(sensor_core_save_all, sensor_core_load_all);

    xTaskCreate(poll_task, "sensor_poll", 4096, NULL, 4, NULL);
    return ESP_OK;
}

// Checks that the driver's tables are consistent and that every fixed-size
// registry has room for them, before anything is written.
static esp_err_t check_registration(const sensor_driver_t *drv)
{
    if (drv->cfgkey_count > SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < drv->cfgkey_count; i++) {
        if (!scl_cfgkey_def_valid(&drv->cfgkeys[i])) {
            ESP_LOGE(TAG, "%s: configuration key %u is inconsistent", drv->type_name, (unsigned)i);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (s_instance_count >= MAX_SENSOR_INSTANCES ||
        s_channel_shim_count + drv->channel_count > MAX_SENSOR_CHANNEL_SHIMS ||
        s_cfgkey_count + drv->cfgkey_count > MAX_SENSOR_CFGKEYS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void register_channels(sensor_inst_t *inst)
{
    const sensor_driver_t *drv = inst->drv;
    for (size_t i = 0; i < drv->channel_count; i++) {
        channel_shim_t *shim = &s_channel_shims[s_channel_shim_count++];
        shim->inst = inst;
        shim->def = &drv->channels[i];
        char full[PROTO_NAME_MAXLEN];
        snprintf(full, sizeof(full), "%s.%s", inst->name, drv->channels[i].name);
        channel_registry_register(full, drv->channels[i].type_name, drv->channels[i].access, shim, channel_shim_get,
                                  drv->channels[i].set != NULL ? channel_shim_set : NULL);
    }
}

static void register_cfgkeys(sensor_inst_t *inst)
{
    const sensor_driver_t *drv = inst->drv;
    for (size_t i = 0; i < drv->cfgkey_count; i++) {
        cfgkey_entry_t *e = &s_cfgkeys[s_cfgkey_count++];
        snprintf(e->full_name, sizeof(e->full_name), "%s.%s", inst->name, drv->cfgkeys[i].name);
        e->inst = inst;
        e->drv = drv;
        e->def = &drv->cfgkeys[i];
    }
}

// probe -> init -> apply saved configuration -> start. A chip that failed a
// step must not be reported as healthy: a half-configured chip can still
// answer I2C reads with plausible numbers from its reset-value registers. See
// AGENTS.md, "Firmware mistakes to avoid".
static void run_lifecycle(sensor_inst_t *inst)
{
    const sensor_driver_t *drv = inst->drv;
    esp_err_t perr = drv->probe(inst);
    esp_err_t ierr = ESP_FAIL;
    esp_err_t serr = ESP_FAIL;
    if (perr == ESP_OK) {
        ierr = drv->init(inst);
        if (ierr == ESP_OK) {
            load_instance_cfg(inst); // applies saved config, or each key's default
            serr = drv->start(inst);
        }
    }
    sensor_inst_state_t outcome = scl_lifecycle_outcome(perr == ESP_OK, ierr == ESP_OK, serr == ESP_OK);
    set_inst_state(inst, outcome);
    if (outcome == SENSOR_INST_RUNNING) {
        ESP_LOGI(TAG, "%s (%s): ready", inst->name, drv->type_name);
    } else {
        ESP_LOGW(TAG, "%s (%s): %s (probe %d, init %d, start %d) -- channels report no reading until this is fixed",
                 inst->name, drv->type_name, scl_state_text(outcome), perr, ierr, serr);
    }
}

esp_err_t sensor_core_register(const sensor_driver_t *drv, const char *instance_name,
                               const sensor_board_binding_t *binding, sensor_inst_t **out_inst)
{
    esp_err_t err = check_registration(drv);
    if (err != ESP_OK) {
        return err;
    }

    sensor_inst_t *inst = &s_instances[s_instance_count++];
    memset(inst, 0, sizeof(*inst));
    strncpy(inst->name, instance_name, sizeof(inst->name) - 1);
    inst->drv = drv;
    inst->binding = *binding;
    inst->state_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    inst->state = SENSOR_INST_PROBING;
    inst->next_poll_ms = 0;

    // Channels and configuration keys go on the wire whether or not probe
    // succeeds -- see sensor_driver_t::probe in sensor_core.h.
    register_channels(inst);
    register_cfgkeys(inst);
    run_lifecycle(inst);

    if (out_inst != NULL) {
        *out_inst = inst;
    }
    return ESP_OK;
}
