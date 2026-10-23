#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cbor.h"
#include "sensor_cfgkey.h"
#include "sensor_inst_state.h"

// Pure logic split out of sensor_core.c so it runs in the host-side unit
// tests (test/host/test_sensor_cfg.c). No ESP-IDF, FreeRTOS, or NVS calls.

// ---- configuration values -------------------------------------------------

// true if the key definition is self-consistent: min <= max for numbers; for
// an enum, a non-NULL value list, a non-zero count, and a default inside it.
bool scl_cfgkey_def_valid(const sensor_cfgkey_def_t *def);

// true if v is a legal stored value for this key (type and range).
bool scl_value_in_range(const sensor_cfgkey_def_t *def, int32_t v);

// Parses one SCFG wire value. Rejects a wrong CBOR type, an integer that does
// not fit in 64 bits signed, an out-of-range number, and an unknown enum name.
bool scl_parse_cfg_value(CborValue *val, const sensor_cfgkey_def_t *def, int32_t *out);

// The text for an enum index, or NULL if the index is out of range.
const char *scl_enum_text(const sensor_cfgkey_def_t *def, int32_t index);

// Joins enum names with commas into out, always NUL-terminated, cutting at cap.
void scl_join_enum_values(const sensor_cfgkey_def_t *def, char *out, size_t cap);

// ---- the saved record ------------------------------------------------------
// Layout: one version byte, then n native-endian int32 values.

size_t scl_blob_len(size_t n);

// Writes the record into buf (which holds at least scl_blob_len(n) bytes).
size_t scl_blob_pack(const int32_t *values, size_t n, uint8_t version, uint8_t *buf);

// Reads a record. If the length or version is wrong, every value is its
// default and the result is false. Otherwise each value that fails
// scl_value_in_range() is replaced by its default alone, and the number of
// replaced values is stored in *replaced (may be NULL).
bool scl_blob_unpack(const uint8_t *buf, size_t got_len, uint8_t version, const sensor_cfgkey_def_t *defs, size_t n,
                     int32_t *out, size_t *replaced);

// ---- names -------------------------------------------------------------

// true if full_name belongs to instance `filter`: it starts with filter and
// the next character is '.'.
bool scl_scaps_filter_match(const char *full_name, const char *filter);

// Splits "inst.action" at the last '.', writing a NUL there. On success,
// *action points after it. Fails if there is no '.', or either side is empty.
bool scl_split_action_key(char *key, const char **action);

// ---- instance state rules ------------------------------------------------

bool scl_state_is_operational(sensor_inst_state_t s);
bool scl_state_can_poll(sensor_inst_state_t s);
bool scl_state_can_write_cfg(sensor_inst_state_t s); // SCFG write, CAL start
const char *scl_state_text(sensor_inst_state_t s);

// The state after registration's probe/init/start steps.
sensor_inst_state_t scl_lifecycle_outcome(bool probe_ok, bool init_ok, bool start_ok);

// The state after a CAL action ends: RUNNING only if the state is still BUSY.
sensor_inst_state_t scl_cal_finish_state(sensor_inst_state_t current);

// The wire error code for a driver's action() failure: ERANGE for a target outside
// its safe range, ENOCH for an action name it does not know, EINTERNAL otherwise.
// `err` is an esp_err_t; the values are passed as ints to keep this file free of ESP-IDF.
const char *scl_cal_error_code(int err, int invalid_arg, int not_found);
