#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Pure validation and sanitization helpers, with no ESP-IDF or FreeRTOS
// dependency, so they can be unit-tested on a host compiler with no
// ESP32 toolchain -- see test/host/README.md. Used wherever a value
// loaded from flash (NVS), or a value already decoded off the wire, must
// be checked before it is trusted. See AGENTS.md, "Firmware mistakes to
// avoid".

// true if min <= value <= max.
bool iv_u32_in_range(uint32_t value, uint32_t min, uint32_t max);
bool iv_i32_in_range(int32_t value, int32_t min, int32_t max);

// true if value is 0 or 1 -- the two valid encodings of a stored bool.
bool iv_bool_in_range(int32_t value);

// true if 0 <= value < enum_count -- a stored or decoded ENUM key's
// valid index range.
bool iv_enum_in_range(int32_t value, size_t enum_count);

// Forces buf to be a NUL-terminated string within its own bufsz bytes,
// by writing '\0' at buf[bufsz - 1]. Safe even if buf was not already
// terminated (for example, a string loaded from a corrupted flash
// record). A no-op if bufsz is 0 or buf is NULL.
void iv_force_terminate(char *buf, size_t bufsz);
