#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Device-level streaming configuration and the subscription list. These are
// the keys the CFG verb covers -- a small, fixed set, distinct from a
// sensor's own configuration (SCFG/SCAPS; see sensor_core.h). See
// docs/07-command-reference.md, "Streaming commands", and
// docs/12-flow-control-and-backpressure.md.

// Loads defaults, then loads saved values from NVS if any exist.
esp_err_t device_config_init(void);

// PERIOD_US: time between samples, in microseconds. 1000 or more.
uint32_t device_config_get_period_us(void);
int device_config_set_period_us(uint32_t value);

// MODE: "PERIODIC", "ONCHANGE", or "BOTH".
const char *device_config_get_mode(void);
int device_config_set_mode(const char *value);

// BATCH: number of sample frames the device groups into one USB write, from
// 1 to 32. This does not change the shape of a sample frame. It only
// changes how often the device flushes its USB transmit buffer.
uint16_t device_config_get_batch(void);
int device_config_set_batch(uint16_t value);

// ENCODING: always "CBOR" in this version. See docs/05-message-model.md.
const char *device_config_get_encoding(void);

// LED_MODE: "STATUS" (the LED shows device state) or "MANUAL" (a host has
// taken control with SET led0). See docs/10-led-status.md. Setting led0
// directly also switches this to "MANUAL"; status_led.c, not this module,
// makes that switch, since it is the one that owns the LED state machine.
const char *device_config_get_led_mode(void);
int device_config_set_led_mode(const char *value);

// Registers the one callback run after a successful device_config_set_led_mode()
// (from CFG LED_MODE), so status_led.c can react immediately rather than
// polling. status_led_init() calls this once; device_proto does not depend
// on status_led, so this is the narrow channel the other way around.
void device_config_register_led_mode_hook(void (*on_change)(const char *mode));

// Updates the stored LED_MODE without validation and without running the
// hook above. status_led.c calls this when it switches to "MANUAL" on its
// own initiative (a host called SET led0), so a later CFG LED_MODE read
// reports the true current mode.
void device_config_note_led_mode_changed_externally(const char *value);

// The subscription list.
int device_config_subscribe(const char *channel_name);
int device_config_unsubscribe(const char *channel_name);
size_t device_config_sub_count(void);
const char *device_config_sub_at(size_t index);

// Streaming state.
bool device_config_is_streaming(void);
int device_config_start_streaming(void);
int device_config_stop_streaming(void);

// Registers an extra persistence hook, run by device_config_save() and
// device_config_load() after the device's own configuration. sensor_core
// uses this to fold per-sensor configuration into the same SAVE/LOAD verbs,
// without device_proto needing to know sensor_core exists. Up to 4 hooks.
esp_err_t device_config_register_persist_hook(esp_err_t (*save)(void), esp_err_t (*load)(void));

// Saves the current configuration and subscription list to NVS, then runs
// every registered persistence hook's save function.
esp_err_t device_config_save(void);

// Loads the configuration and subscription list from NVS, replacing the
// current values, then runs every registered persistence hook's load
// function. Returns ESP_ERR_NVS_NOT_FOUND if nothing was saved for the
// device-level configuration; a hook with nothing saved is not an error.
esp_err_t device_config_load(void);
