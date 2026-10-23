#pragma once

#include <stdbool.h>

// A sensor instance's lifecycle. See docs/20-state-machines.md and
// docs/09-sensor-configuration.md, "SENS". Free of ESP-IDF headers so the
// transition rules in sensor_cfg_logic.c run in the host-side unit tests.
//
//   PROBING -> PROBE_FAILED
//           -> (probe ok) -> INIT_FAILED
//                          -> (init ok) -> START_FAILED
//                                       -> (start ok) -> RUNNING <-> BUSY
//
// PROBE_FAILED, INIT_FAILED, START_FAILED, and FAULT are terminal: this
// version does not retry or re-probe on its own, so each needs a restart.
typedef enum {
    // Must stay the first (zero) value: sensor_core_register() takes a new
    // instance's array slot and only then memset()s it, so the poll task --
    // already running on a different task -- can observe this slot for a
    // brief window before its state is explicitly set. Zero-initialized
    // memory must already mean "do not poll this yet", which PROBING does.
    SENSOR_INST_PROBING,      // momentary: set before probe() runs
    SENSOR_INST_PROBE_FAILED, // probe() returned an error
    SENSOR_INST_INIT_FAILED,  // probe() ok, init() returned an error
    SENSOR_INST_START_FAILED, // probe()+init() ok, start() returned an error
    SENSOR_INST_RUNNING,      // normal operation
    SENSOR_INST_BUSY,         // a CAL action owns the chip right now
    SENSOR_INST_FAULT,        // the driver gave up trusting its own data
} sensor_inst_state_t;

_Static_assert(SENSOR_INST_PROBING == 0, "PROBING must be the zero value");
