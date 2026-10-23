# The VL53L1X sensor module: a sensor_driver_t implementation built on the
# vendored ULD. See docs/03-sensor-vl53l1x.md and docs/15-adding-a-sensor.md.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        sensor_core
    PRIV_REQUIRES
        vl53l1x_uld
        freertos
)
