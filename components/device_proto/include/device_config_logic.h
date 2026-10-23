#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The stored device configuration, and the rules that make a loaded copy of
// it safe to use. Free of ESP-IDF headers so the host-side unit tests
// (test/host/test_device_config.c) run them directly.

#define DC_CFG_VERSION         1
#define DC_MAX_SUBS            8
#define DC_MODE_MAXLEN         16
#define DC_LED_MODE_MAXLEN     8
#define DC_CHANNEL_NAME_MAXLEN 40

#define DC_PERIOD_US_MIN     1000u
#define DC_PERIOD_US_MAX     3600000000u // 1 hour; the same cap the CFG command applies
#define DC_PERIOD_US_DEFAULT 100000u     // 10 Hz
#define DC_BATCH_MIN         1
#define DC_BATCH_MAX         32

typedef struct {
    uint8_t version;
    uint32_t period_us;
    char mode[DC_MODE_MAXLEN];
    uint16_t batch;
    char led_mode[DC_LED_MODE_MAXLEN];
    char subs[DC_MAX_SUBS][DC_CHANNEL_NAME_MAXLEN];
    uint32_t sub_count;
    bool streaming;
} config_state_t;

void dc_set_defaults(config_state_t *c);

bool dc_mode_valid(const char *mode);     // PERIODIC, ONCHANGE, or BOTH
bool dc_led_mode_valid(const char *mode); // STATUS or MANUAL

// Makes a configuration loaded from flash safe to use. Every string is
// terminated, every number is in range (a bad one is replaced by its
// default), the subscription count is clamped, and streaming is cleared.
// Returns the number of fields it had to repair.
int dc_sanitize_loaded(config_state_t *c);
