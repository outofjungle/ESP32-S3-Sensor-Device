# Board pin and bus knowledge: one profile header per board, selected at
# build time by the BOARD_* define the root CMakeLists.txt sets. See
# docs/16-adding-a-device.md.
# board.h is a public header that names gpio_num_t, i2c_master_bus_handle_t,
# and SemaphoreHandle_t in its API, so esp_driver_gpio, esp_driver_i2c, and
# freertos must be public REQUIRES: any component that includes board.h
# (status_led, main) needs those types resolved too, not just board.c's
# own sources.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        freertos
)
