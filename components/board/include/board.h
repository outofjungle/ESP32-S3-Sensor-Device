#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// One board's pin and bus knowledge, in one place, so a sensor module
// never touches a GPIO number directly -- it asks the board for a bus
// handle or a pin. Porting to a new board is a new header here plus one
// line in board.c, with nothing else in the tree changed. See
// docs/01-architecture.md and docs/16-adding-a-device.md.
//
// The active profile is chosen at build time by a BOARD_* define the root
// CMakeLists.txt sets from the BOARD make variable (default atoms3_lite).
// Exactly one profile header is compiled in.

typedef struct {
    const char *name; // "atoms3_lite"

    // The Grove port's I2C pins. gpio_num_t is -1 (GPIO_NUM_NC) if this
    // board has no Grove port.
    gpio_num_t i2c_sda;
    gpio_num_t i2c_scl;
    uint32_t i2c_freq_hz;

    // The onboard status LED's data pin, or GPIO_NUM_NC if this board has
    // none. status_led.c compiles to no-ops when this is GPIO_NUM_NC.
    gpio_num_t led_gpio;

    // The onboard button, or GPIO_NUM_NC if this board has none.
    gpio_num_t button_gpio;
    bool button_active_low; // true: the pin reads low when pressed
} board_profile_t;

// Returns the active board's profile. Never NULL.
const board_profile_t *board_get_profile(void);

// Creates, on the first call, the I2C master bus on the board's Grove
// port pins, with internal pull-ups enabled (a sensor unit typically
// carries its own, so this is belt-and-braces, not a substitute for
// correct wiring), and a mutex to serialize access to it. A later call
// returns the same cached bus and mutex -- a second Grove sensor on the
// same port must call this again and reuse both, rather than create a
// second I2C peripheral on the same two pins, which fails. See
// docs/16-adding-a-device.md and AGENTS.md, "Firmware mistakes to
// avoid". Fails with ESP_ERR_NOT_SUPPORTED if the board profile has no
// I2C pins.
esp_err_t board_i2c_bus_init(i2c_master_bus_handle_t *out_bus, SemaphoreHandle_t *out_mutex);
