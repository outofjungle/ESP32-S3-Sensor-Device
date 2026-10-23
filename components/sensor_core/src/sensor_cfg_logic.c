#include "sensor_cfg_logic.h"
#include "input_validate.h"
#include "proto_util.h"
#include <string.h>

bool scl_cfgkey_def_valid(const sensor_cfgkey_def_t *def)
{
    if (def == NULL || def->name == NULL) {
        return false;
    }
    switch (def->type) {
    case SENSOR_VAL_ENUM:
        return def->enum_values != NULL && def->enum_count > 0 && iv_enum_in_range(def->default_value, def->enum_count);
    case SENSOR_VAL_BOOL:
        return iv_bool_in_range(def->default_value);
    case SENSOR_VAL_UINT:
    case SENSOR_VAL_INT:
        return def->min <= def->max && iv_i32_in_range(def->default_value, def->min, def->max);
    default:
        return false;
    }
}

bool scl_value_in_range(const sensor_cfgkey_def_t *def, int32_t v)
{
    switch (def->type) {
    case SENSOR_VAL_BOOL:
        return iv_bool_in_range(v);
    case SENSOR_VAL_ENUM:
        return iv_enum_in_range(v, def->enum_count);
    case SENSOR_VAL_UINT:
    case SENSOR_VAL_INT:
    default:
        return iv_i32_in_range(v, def->min, def->max);
    }
}

bool scl_parse_cfg_value(CborValue *val, const sensor_cfgkey_def_t *def, int32_t *out)
{
    switch (def->type) {
    case SENSOR_VAL_BOOL: {
        if (!cbor_value_is_boolean(val)) {
            return false;
        }
        bool b = false;
        cbor_value_get_boolean(val, &b);
        *out = b ? 1 : 0;
        return true;
    }
    case SENSOR_VAL_ENUM: {
        if (!cbor_value_is_text_string(val)) {
            return false;
        }
        char buf[24];
        CborValue tmp;
        if (!proto_copy_cbor_text(val, buf, sizeof(buf), &tmp)) {
            return false;
        }
        for (size_t i = 0; i < def->enum_count; i++) {
            if (strcmp(buf, def->enum_values[i]) == 0) {
                *out = (int32_t)i;
                return true;
            }
        }
        return false;
    }
    case SENSOR_VAL_UINT:
    case SENSOR_VAL_INT:
    default: {
        if (!cbor_value_is_integer(val)) {
            return false;
        }
        // The checked form fails for a value outside int64 (an unsigned
        // above INT64_MAX, or a negative below INT64_MIN). The plain form
        // wraps such a value silently, and the wrapped value can land
        // inside [min, max].
        int64_t v;
        if (cbor_value_get_int64_checked(val, &v) != CborNoError) {
            return false;
        }
        if (v < def->min || v > def->max) {
            return false;
        }
        *out = (int32_t)v;
        return true;
    }
    }
}

const char *scl_enum_text(const sensor_cfgkey_def_t *def, int32_t index)
{
    if (def->type != SENSOR_VAL_ENUM || def->enum_values == NULL || !iv_enum_in_range(index, def->enum_count)) {
        return NULL;
    }
    return def->enum_values[index];
}

void scl_join_enum_values(const sensor_cfgkey_def_t *def, char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    for (size_t i = 0; i < def->enum_count; i++) {
        if (i > 0) {
            strncat(out, ",", cap - strlen(out) - 1);
        }
        strncat(out, def->enum_values[i], cap - strlen(out) - 1);
    }
}

size_t scl_blob_len(size_t n)
{
    return 1 + 4 * n;
}

size_t scl_blob_pack(const int32_t *values, size_t n, uint8_t version, uint8_t *buf)
{
    buf[0] = version;
    for (size_t i = 0; i < n; i++) {
        memcpy(&buf[1 + 4 * i], &values[i], 4);
    }
    return scl_blob_len(n);
}

bool scl_blob_unpack(const uint8_t *buf, size_t got_len, uint8_t version, const sensor_cfgkey_def_t *defs, size_t n,
                     int32_t *out, size_t *replaced)
{
    size_t bad = 0;
    bool valid = (got_len == scl_blob_len(n)) && buf != NULL && buf[0] == version;
    for (size_t i = 0; i < n; i++) {
        if (!valid) {
            out[i] = defs[i].default_value;
            continue;
        }
        int32_t v;
        memcpy(&v, &buf[1 + 4 * i], 4);
        if (!scl_value_in_range(&defs[i], v)) {
            v = defs[i].default_value;
            bad++;
        }
        out[i] = v;
    }
    if (replaced != NULL) {
        *replaced = bad;
    }
    return valid;
}

bool scl_scaps_filter_match(const char *full_name, const char *filter)
{
    size_t flen = strlen(filter);
    return strncmp(full_name, filter, flen) == 0 && full_name[flen] == '.';
}

bool scl_split_action_key(char *key, const char **action)
{
    char *dot = strrchr(key, '.');
    if (dot == NULL || dot == key || dot[1] == '\0') {
        return false;
    }
    *dot = '\0';
    *action = dot + 1;
    return true;
}

bool scl_state_is_operational(sensor_inst_state_t s)
{
    return s == SENSOR_INST_RUNNING || s == SENSOR_INST_BUSY;
}

bool scl_state_can_poll(sensor_inst_state_t s)
{
    return s == SENSOR_INST_RUNNING;
}

bool scl_state_can_write_cfg(sensor_inst_state_t s)
{
    return s == SENSOR_INST_RUNNING;
}

const char *scl_state_text(sensor_inst_state_t s)
{
    switch (s) {
    case SENSOR_INST_RUNNING:
        return "ok";
    case SENSOR_INST_BUSY:
        return "busy";
    case SENSOR_INST_INIT_FAILED:
        return "init_failed";
    case SENSOR_INST_START_FAILED:
        return "start_failed";
    case SENSOR_INST_PROBING: // momentary; unobservable from a command
    case SENSOR_INST_PROBE_FAILED:
    case SENSOR_INST_FAULT:
    default:
        return "fault";
    }
}

sensor_inst_state_t scl_lifecycle_outcome(bool probe_ok, bool init_ok, bool start_ok)
{
    if (!probe_ok) {
        return SENSOR_INST_PROBE_FAILED;
    }
    if (!init_ok) {
        return SENSOR_INST_INIT_FAILED;
    }
    if (!start_ok) {
        return SENSOR_INST_START_FAILED;
    }
    return SENSOR_INST_RUNNING;
}

sensor_inst_state_t scl_cal_finish_state(sensor_inst_state_t current)
{
    return current == SENSOR_INST_BUSY ? SENSOR_INST_RUNNING : current;
}

const char *scl_cal_error_code(int err, int invalid_arg, int not_found)
{
    if (err == invalid_arg) {
        return "ERANGE";
    }
    return err == not_found ? "ENOCH" : "EINTERNAL";
}
