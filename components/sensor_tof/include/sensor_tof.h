#pragma once

#include "esp_err.h"
#include "sensor_core.h"

// Registers one VL53L1X time-of-flight sensor instance. See
// docs/03-sensor-vl53l1x.md, docs/08-channel-model-and-types.md, and
// docs/09-sensor-configuration.md for what it exposes.
//
// @param[in] instance_name e.g. "tof0". Joined with "." to name every
//     channel and configuration key this instance exposes (tof0.dist,
//     tof0.dist_mode, ...). Also used as this instance's NVS key, so keep
//     it to 15 bytes or fewer.
// @param[in] binding The I2C bus and 7-bit address to reach it on (0x29 by
//     default; see docs/03-sensor-vl53l1x.md if it was changed).
esp_err_t sensor_tof_register(const char *instance_name, const sensor_board_binding_t *binding);

// The first registered instance's last-polled presence value: true if the
// configured window condition currently holds, false if it does not or if
// window detection is off, or if no instance is registered. A device's
// app_main.c uses this for the status LED's STREAM_IDLE/STREAM_PRESENT
// split; see docs/10-led-status.md. GET tof0.present (not this) is the
// correct way for a host to read presence.
bool sensor_tof_is_present(void);
