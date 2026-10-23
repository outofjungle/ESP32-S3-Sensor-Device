#pragma once

// Device assembly: M5Stack AtomS3 Lite + one Grove-port sensor. See
// docs/02-board-atoms3-lite.md and docs/16-adding-a-device.md.
//
// Which board profile and which sensor module were compiled in is decided
// at build time (make build BOARD=atoms3_lite SENSOR=tof), not here --
// board.c and the SENSOR_* define in app_main.c's #if guards handle that.
// This file only names the things this particular device assembly chooses
// to call them.

#define DEVICE_DISPLAY_NAME "ESP32-S3 Sensor Device"
#define TOF_INSTANCE_NAME   "tof0"
#define TOF_I2C_ADDR        0x29
#define LDC_INSTANCE_NAME   "ldc0"
#define LDC_I2C_ADDR        0x2B
