#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "channel_registry.h"

// Pure parts of device_stream.c, split out so the host-side unit tests
// (test/host/test_stream_logic.c) run them.

#define STREAM_SEQ_MAX 65535

// Returns the current sequence number and advances it, wrapping 65535 -> 0.
// A value already above the maximum restarts at 0.
uint32_t stream_seq_next(uint32_t *seq);

// Builds a sample frame [4, seq, ts_us, name, value, flags] into buf.
// Returns false, with *len untouched, if it did not fit or the channel's
// get() left the frame incomplete -- the caller sends nothing then.
bool stream_build_sample(uint8_t *buf, size_t cap, uint32_t seq, uint64_t ts_us, const channel_entry_t *ch,
                         uint32_t flags, size_t *len);
