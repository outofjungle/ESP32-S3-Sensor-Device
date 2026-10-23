#include "proto_util.h"

bool proto_copy_cbor_text(CborValue *it, char *buf, size_t bufsz, CborValue *next)
{
    if (bufsz == 0) {
        return false;
    }
    // tinycbor's own cbor_value_copy_text_string() asserts (aborts, in a
    // debug build; undefined behavior otherwise) if *it is not a text
    // string -- it does not return an error code for that case. Every
    // call site in this codebase already checks cbor_value_is_text_string()
    // first, but this function must not depend on every future call site
    // remembering to. See AGENTS.md, "Firmware mistakes to avoid".
    if (!cbor_value_is_text_string(it)) {
        return false;
    }
    size_t len = bufsz;
    if (cbor_value_copy_text_string(it, buf, &len, next) != CborNoError) {
        return false;
    }
    if (len >= bufsz) {
        // The string exactly filled or exceeded buf. tinycbor still
        // reports success here but leaves no room for a NUL terminator --
        // reject rather than trust an un-terminated buffer. See
        // AGENTS.md, "Firmware mistakes to avoid".
        return false;
    }
    buf[len] = '\0';
    return true;
}

bool proto_parse_key_value_args(CborValue *it, char *key, size_t key_size, bool *has_value, CborValue *value)
{
    CborValue after_key;
    if (cbor_value_at_end(it) || !proto_copy_cbor_text(it, key, key_size, &after_key)) {
        return false;
    }
    *has_value = !cbor_value_at_end(&after_key);
    *value = after_key;
    if (!*has_value) {
        return true;
    }
    CborValue after_value = after_key;
    return cbor_value_advance(&after_value) == CborNoError && cbor_value_at_end(&after_value);
}
