# vl53l1x_platform_esp.h is a public header that names
# i2c_master_bus_handle_t, so esp_driver_i2c must be a public REQUIRES: a
# consumer including that header (sensor_tof) needs the type resolved too.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
    PRIV_REQUIRES
        freertos
)
