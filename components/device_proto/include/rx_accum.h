#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

// Collects bytes from the host into frames, splitting at the 0x00
// delimiter. Free of ESP-IDF headers so the host-side unit tests
// (test/host/test_rx_accum.c) run it directly. See docs/20-state-machines.md,
// "USB command frame receiver".
//
// States: EMPTY (len == 0), ACCUMULATING (0 < len <= PROTO_DECODE_CAP - 1),
// and OVERFLOW (the frame is longer than the buffer; bytes are dropped until
// the delimiter).

typedef struct {
    uint8_t buf[PROTO_DECODE_CAP];
    size_t len;
    bool overflow;
} rx_accum_t;

typedef struct {
    // A complete frame body (the bytes before the delimiter, 1 or more).
    void (*on_frame)(void *ctx, const uint8_t *wire, size_t len);
    // A frame longer than PROTO_DECODE_CAP - 1 bytes. Nothing is decoded.
    void (*on_oversize)(void *ctx);
} rx_accum_cb_t;

void rx_accum_init(rx_accum_t *a);

// Feeds n bytes. Calls a callback once per complete frame. A stray
// delimiter (an empty frame) is ignored: no callback.
void rx_accum_feed(rx_accum_t *a, const uint8_t *bytes, size_t n, const rx_accum_cb_t *cb, void *ctx);
