#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// ESP-IDF I2C transport for the vendored VL53L1X Ultra Lite Driver (ULD).
// See VENDORED.md and docs/03-sensor-vl53l1x.md.
//
// The ULD's VL53L1X_* functions take a uint16_t "dev" parameter and never
// open a bus themselves; they call the nine platform functions declared in
// vl53l1_platform.h (VL53L1_WrByte, VL53L1_RdWord, and so on), which this
// file implements. This header lets a sensor module treat "dev" as plain
// data: attach the I2C address once, then pass that same address to every
// VL53L1X_* call.

// Attaches a VL53L1X at dev_addr (its 7-bit I2C address) on bus, so the
// platform functions can reach it. Call once per sensor instance, after the
// board component has created the I2C bus, and before any VL53L1X_* call
// that uses dev_addr.
esp_err_t vl53l1x_platform_attach(i2c_master_bus_handle_t bus, uint16_t dev_addr);

// Detaches a device attached with vl53l1x_platform_attach, freeing its slot.
esp_err_t vl53l1x_platform_detach(uint16_t dev_addr);
