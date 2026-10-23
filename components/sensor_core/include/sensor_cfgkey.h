#pragma once

#include <stddef.h>
#include <stdint.h>

// The configuration key types a sensor module declares. Kept free of ESP-IDF
// headers so the logic in sensor_cfg_logic.c compiles and runs in the
// host-side unit tests (test/host). sensor_core.h includes this file.

typedef enum {
    SENSOR_VAL_UINT,
    SENSOR_VAL_INT,
    SENSOR_VAL_BOOL,
    SENSOR_VAL_ENUM,
} sensor_val_type_t;

typedef struct {
    const char *name; // unqualified, e.g. "dist_mode" -- joined as "tof0.dist_mode"
    sensor_val_type_t type;
    int32_t min;                    // ignored for ENUM (the valid range is 0..enum_count-1)
    int32_t max;                    // ignored for ENUM
    const char *const *enum_values; // NULL unless type is ENUM
    size_t enum_count;              // 0 unless type is ENUM
    int32_t default_value;          // for ENUM, an index into enum_values
    const char *unit;               // may be NULL
} sensor_cfgkey_def_t;
