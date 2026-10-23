#pragma once

#include "esp_err.h"
#include "sensor_core.h"

// Registers one LDC1612 2-channel inductive sensor instance. See
// docs/19-sensor-ldc1612.md, docs/08-channel-model-and-types.md, and
// docs/09-sensor-configuration.md for what it exposes.
//
// @param[in] instance_name e.g. "ldc0". Joined with "." to name every
//     channel and configuration key this instance exposes (ldc0.ch0_raw,
//     ldc0.ch0_rcount, ...). Also used as this instance's NVS key, so keep
//     it to 15 bytes or fewer.
// @param[in] binding The I2C bus and 7-bit address to reach it on (0x2B by
//     default, the Grove unit's ADDR-pin-high address; see
//     docs/19-sensor-ldc1612.md).
esp_err_t sensor_ldc_register(const char *instance_name, const sensor_board_binding_t *binding);

// The first registered instance's last-polled presence value: true if
// either channel's configured window condition currently holds, false if
// neither does or both are off, or if no instance is registered. A
// device's app_main.c uses this for the status LED's
// STREAM_IDLE/STREAM_PRESENT split; see docs/10-led-status.md. GET
// ldc0.ch0_present / ldc0.ch1_present (not this) is the correct way for a
// host to read presence.
bool sensor_ldc_is_present(void);
