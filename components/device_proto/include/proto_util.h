#pragma once

#include "cbor.h"
#include <stddef.h>
#include <stdbool.h>

// Copies a CBOR text string out of *it into buf, leaving buf always
// NUL-terminated on success. Rejects (returns false) a string that would
// exactly fill or exceed bufsz, rather than silently leaving buf
// un-terminated: tinycbor's own cbor_value_copy_text_string() reports
// CborNoError in that case too, with no terminator written. See AGENTS.md,
// "Firmware mistakes to avoid", item 1. Use this for every CBOR string
// copy in this codebase; do not call cbor_value_copy_text_string()
// directly.
//
// @param[in] it The value to copy. Not advanced. May be any CBOR value,
//     not only a text string -- returns false for anything else, rather
//     than relying on the caller to check cbor_value_is_text_string()
//     first (tinycbor's own copy function asserts on that precondition
//     instead of returning an error).
// @param[out] buf Destination. Left unspecified on a false return.
// @param[in] bufsz Capacity of buf, including room for the NUL.
// @param[out] next Set to the position after the string, as
//     cbor_value_copy_text_string() would. May be the same pointer as it.
//     Left unspecified on a false return.
// @return true on success, false if *it is not a text string, on a
//     decode error, or for a string that does not fit with room for a
//     NUL terminator.
bool proto_copy_cbor_text(CborValue *it, char *buf, size_t bufsz, CborValue *next);

// Parses the argument list of a "<key> [value]" command (CFG, SCFG): a text key,
// then at most one value of any type.
//
// @param[in] it The first argument. Not advanced.
// @param[out] key Receives the key, always terminated.
// @param[in] key_size Capacity of key, including the terminator.
// @param[out] has_value true if a value follows the key.
// @param[out] value Positioned at the value when has_value is true.
// @return false if the key is missing, not text, too long, or if more than one value follows.
bool proto_parse_key_value_args(CborValue *it, char *key, size_t key_size, bool *has_value, CborValue *value);
