#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cbor.h"
#include "protocol.h"

// A channel is one named source of data, or one named control, on the
// device. See docs/08-channel-model-and-types.md. This registry holds every
// channel on the device: the ones the device itself owns (led0, uptime_us)
// and the ones a sensor module registers (tof0.dist, tof0.present, ...).
// device_proto knows nothing about what a channel represents -- only its
// name, its declared type, its access mode, and how to read or write it.
//
// A channel's get()/set() callbacks take an opaque ctx pointer, supplied at
// registration and passed back unchanged on every call. A sensor module
// uses this to pass its own instance handle without device_proto needing to
// know what that handle is.

typedef esp_err_t (*channel_get_fn)(void *ctx, CborEncoder *enc);
typedef int (*channel_set_fn)(void *ctx, CborValue *val); // returns 0, or one of err_codes.h

typedef struct {
    char name[PROTO_NAME_MAXLEN];
    char type_name[12]; // "bool", "uint", "int", "bytes" -- shown in a CAPS row
    char access[4];     // "RO", "WO", or "RW"
    void *ctx;
    channel_get_fn get;
    channel_set_fn set; // NULL for a read-only channel
} channel_entry_t;

// The largest number of channels this registry holds at once. Comfortably
// above what one sensor instance plus the device's own channels need; see
// docs/08-channel-model-and-types.md for the channel count in this version.
#define CHANNEL_REGISTRY_MAX 32

// Registers one channel. name must be unique in the registry (a device-level
// name such as "led0", or a dotted instance name such as "tof0.dist" -- this
// function does not care which). set may be NULL for a read-only channel.
//
// @return ESP_OK; ESP_ERR_INVALID_ARG for a NULL argument, a bad access
//     string, or a name (type name) that does not fit with its NUL;
//     ESP_ERR_INVALID_STATE if the name is already registered; or
//     ESP_ERR_NO_MEM if the registry is full.
esp_err_t channel_registry_register(const char *name, const char *type_name, const char *access, void *ctx,
                                    channel_get_fn get, channel_set_fn set);

// Returns the number of registered channels.
size_t channel_registry_count(void);

// Returns the channel at this position, in registration order. index must
// be less than channel_registry_count().
const channel_entry_t *channel_registry_at(size_t index);

// Finds a channel by its full name. Returns NULL if no channel has this name.
const channel_entry_t *channel_registry_find(const char *name);
