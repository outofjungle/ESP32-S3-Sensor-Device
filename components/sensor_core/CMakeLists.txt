# THE LIBRARY: the sensor_driver_t interface, the instance and
# configuration-key registries, the poll task, and the SENS/SCAPS/SCFG/CAL
# verbs. Depends on device_proto (for the channel registry, device_config,
# and the verb dispatcher) but not on board -- a sensor_board_binding_t
# arrives already filled in from whoever calls sensor_core_register(). See
# docs/01-architecture.md and docs/15-adding-a-sensor.md.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        tinycbor
        esp_driver_i2c
    PRIV_REQUIRES
        device_proto
        nvs_flash
        esp_timer
        freertos
)
