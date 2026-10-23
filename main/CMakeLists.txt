# Selects one device assembly's source directory and the sensor component
# it was built with. DEVICE and SENSOR come from the environment -- the
# same DEVICE/BOARD/SENSOR make variables the Makefile exports with
# `docker-compose run -e`. This file reads them independently of the root
# CMakeLists.txt's own copy, rather than relying on a CMake variable
# crossing between the two: ESP-IDF resolves this file's PRIV_REQUIRES in
# an early pass that does not carry the root file's CMake variables. See
# docs/01-architecture.md and docs/16-adding-a-device.md.
if(NOT DEFINED ENV{DEVICE} OR "$ENV{DEVICE}" STREQUAL "")
    set(DEVICE "atoms3_lite")
else()
    set(DEVICE "$ENV{DEVICE}")
endif()
if(NOT DEFINED ENV{SENSOR} OR "$ENV{SENSOR}" STREQUAL "")
    set(SENSOR "tof")
else()
    set(SENSOR "$ENV{SENSOR}")
endif()

set(MAIN_PRIV_REQUIRES board device_proto sensor_core status_led esp_driver_gpio esp_timer nvs_flash)
if(NOT SENSOR STREQUAL "none")
    list(APPEND MAIN_PRIV_REQUIRES "sensor_${SENSOR}")
endif()

idf_component_register(
    SRC_DIRS "devices/${DEVICE}"
    INCLUDE_DIRS "devices/${DEVICE}"
    PRIV_REQUIRES ${MAIN_PRIV_REQUIRES}
)
