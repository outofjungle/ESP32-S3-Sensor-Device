# ESP32-S3 Sensor Device - CMakeLists.txt
#
# Build-time selection: which device assembly (main/devices/<DEVICE>/), which
# board profile (components/board/include/board_<BOARD>.h), and which sensor
# module (components/sensor_<SENSOR>/). Defaults build the M5Stack AtomS3
# Lite with the VL53L1X ToF sensor. See docs/01-architecture.md and
# docs/14-build-and-flash.md.
#
# DEVICE, BOARD, and SENSOR are set from the Makefile's DEVICE/BOARD/SENSOR
# variables via `idf.py -D DEVICE=... -D BOARD=... -D SENSOR=...`; the
# defaults below only apply to a bare `idf.py build` run outside the
# Makefile. SENSOR=none builds no sensor module at all -- see
# docs/16-adding-a-device.md for what that is useful for.

cmake_minimum_required(VERSION 3.22)

# These come from the environment, not a CMake -D cache variable: ESP-IDF
# resolves each component's PRIV_REQUIRES (main/CMakeLists.txt's included)
# in an early pass that does not carry this file's CMake variables, cache
# entries included -- confirmed by testing, not by the CMake documentation,
# which describes no such isolation. An environment variable is inherited
# by every process regardless, so it is the one thing both this file and
# main/CMakeLists.txt can read identically. The Makefile exports these via
# `docker-compose run -e`, not `idf.py -D`. See docs/14-build-and-flash.md.
if(DEFINED ENV{DEVICE} AND NOT "$ENV{DEVICE}" STREQUAL "")
    set(DEVICE "$ENV{DEVICE}")
else()
    set(DEVICE "atoms3_lite")
endif()
if(DEFINED ENV{BOARD} AND NOT "$ENV{BOARD}" STREQUAL "")
    set(BOARD "$ENV{BOARD}")
else()
    set(BOARD "atoms3_lite")
endif()
if(DEFINED ENV{SENSOR} AND NOT "$ENV{SENSOR}" STREQUAL "")
    set(SENSOR "$ENV{SENSOR}")
else()
    set(SENSOR "tof")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main/devices/${DEVICE}")
    message(FATAL_ERROR "No such device '${DEVICE}'. Looked for main/devices/${DEVICE}/. "
                         "See docs/16-adding-a-device.md.")
endif()
if(NOT SENSOR STREQUAL "none" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/sensor_${SENSOR}")
    message(FATAL_ERROR "No such sensor module 'sensor_${SENSOR}'. Looked for "
                         "components/sensor_${SENSOR}/. Set SENSOR=none to build with no sensor "
                         "module. See docs/15-adding-a-sensor.md.")
endif()

string(TOUPPER "${BOARD}" BOARD_UPPER)
string(TOUPPER "${SENSOR}" SENSOR_UPPER)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only components reachable from main's own requirements are built -- an
# unselected sensor module costs no flash and no build time, not merely a
# disabled one. See docs/14-build-and-flash.md.
idf_build_set_property(MINIMAL_BUILD ON)

idf_build_set_property(COMPILE_DEFINITIONS "-DBOARD_${BOARD_UPPER}=1" APPEND)
if(NOT SENSOR STREQUAL "none")
    idf_build_set_property(COMPILE_DEFINITIONS "-DSENSOR_${SENSOR_UPPER}=1" APPEND)
endif()

set(PROJECT_VER "0.1")
set(PROJECT_VER_NUMBER 1)

project(ESP32-S3-Sensor-Device)
