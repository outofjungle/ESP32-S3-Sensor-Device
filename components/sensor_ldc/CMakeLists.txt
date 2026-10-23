# The LDC1612 sensor module: a sensor_driver_t implementation. No vendored
# driver component -- the register set is small enough that sensor_ldc.c
# reaches the chip directly over I2C. See docs/19-sensor-ldc1612.md and
# docs/15-adding-a-sensor.md.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        sensor_core
    PRIV_REQUIRES
        esp_driver_i2c
        freertos
)
