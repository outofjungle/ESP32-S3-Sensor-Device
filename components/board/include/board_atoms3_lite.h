#pragma once

// M5Stack AtomS3 Lite (ESP32-S3FN8). See docs/02-board-atoms3-lite.md.
//
// | Pin  | Function                        |
// |------|----------------------------------|
// | G1   | Grove port, SCL (white wire)     |
// | G2   | Grove port, SDA (yellow wire)    |
// | G35  | RGB LED data (WS2812C-2020, x1)  |
// | G41  | Push button (pulled high, active low) |
//
// The Grove connector's 4th and 5th pins are 5V and GND; they carry no
// GPIO number and are not part of this profile. The sensor unit's own
// datasheet describes the HY2.0-4P pinout (black GND, red 5V, yellow SDA,
// white SCL); see docs/03-sensor-vl53l1x.md.

static const board_profile_t s_board_profile = {
    .name = "atoms3_lite",
    .i2c_sda = GPIO_NUM_2,
    .i2c_scl = GPIO_NUM_1,
    .i2c_freq_hz = 400000,
    .led_gpio = GPIO_NUM_35,
    .button_gpio = GPIO_NUM_41,
    .button_active_low = true,
};
