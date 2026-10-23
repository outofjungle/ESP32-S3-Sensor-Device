#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cbor.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sensor_cfgkey.h"

// The sensor library: the interface a Grove sensor module implements
// (sensor_driver_t), the instance and configuration-key registries behind
// the SENS, SCAPS, SCFG, and CAL verbs, and the one task that polls every
// registered sensor on its own cadence. See docs/01-architecture.md and
// docs/15-adding-a-sensor.md -- this header is that contract.
//
// sensor_core sits between device_proto (which knows nothing about
// sensors) and a sensor module such as sensor_tof (which knows nothing
// about the wire protocol). A sensor module never calls usb_cdc.h or
// proto_reply.h directly.

typedef struct sensor_inst sensor_inst_t; // opaque; defined in sensor_core.c

// Maps a tinycbor encode result to the esp_err_t a channel get() returns.
static inline esp_err_t sensor_cbor_to_esp(CborError err)
{
    return err == CborNoError ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// Channels: GET/SET/CAPS/streaming. See docs/08-channel-model-and-types.md.
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;      // unqualified, e.g. "dist" -- joined as "tof0.dist"
    const char *type_name; // "uint", "bool", "bytes" -- shown in a CAPS row
    const char *access;    // "RO" or "RW"
    esp_err_t (*get)(sensor_inst_t *inst, CborEncoder *enc);
    int (*set)(sensor_inst_t *inst, CborValue *val); // NULL if access is "RO"
} sensor_channel_def_t;

// ---------------------------------------------------------------------------
// Configuration keys: SCFG/SCAPS/CAL. See docs/09-sensor-configuration.md.
// ---------------------------------------------------------------------------

// The key types (sensor_val_type_t, sensor_cfgkey_def_t) live in
// sensor_cfgkey.h, which has no ESP-IDF includes so host tests can use it.

// The most configuration keys one sensor_driver_t may declare. A driver
// with more than this cannot be registered; sensor_core_register() returns
// ESP_ERR_INVALID_SIZE. Also the width of one instance's NVS blob -- see
// docs/09-sensor-configuration.md's persistence section.
//
// Raised from 16 to 24 for sensor_ldc (22 keys). Each instance's flash
// record sizes itself from the driver's own cfgkey_count, not from this
// constant, so raising it does not change the on-flash layout of an
// instance that already has fewer keys -- a saved tof0 record (14 keys)
// stays valid. See docs/15-adding-a-sensor.md.
#define SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER 24

// ---------------------------------------------------------------------------
// The driver contract
// ---------------------------------------------------------------------------

typedef struct {
    const char *type_name; // "vl53l1x" -- shown in a SENS row

    const sensor_channel_def_t *channels;
    size_t channel_count;
    const sensor_cfgkey_def_t *cfgkeys;
    size_t cfgkey_count;

    // Is the chip really there, and is it the expected part? Called once,
    // at registration. A failure here still leaves the instance's channels
    // and configuration keys visible on the wire (so a host can tell what
    // is supposed to be attached), but skips init/start and the poll
    // schedule, and reports state "fault" in a SENS row.
    esp_err_t (*probe)(sensor_inst_t *inst);

    // One-time setup after a successful probe.
    esp_err_t (*init)(sensor_inst_t *inst);

    // Begins continuous sampling.
    esp_err_t (*start)(sensor_inst_t *inst);

    // Ends continuous sampling. Not currently called by sensor_core except
    // ahead of a CAL action, which needs ranging stopped first on this
    // chip family; see docs/09-sensor-configuration.md.
    esp_err_t (*stop)(sensor_inst_t *inst);

    // Runs one poll cycle: read the sensor, update whatever cached sample
    // the driver's channel get() callbacks read from. Called on
    // sensor_core's own poll task, never on the USB RX task or the
    // device_proto sample timer -- see docs/15-adding-a-sensor.md, "A
    // channel's get() must not block".
    esp_err_t (*poll)(sensor_inst_t *inst);

    // How often to call poll(), in milliseconds. May change at runtime (a
    // config key can change the sensor's own measurement cadence); called
    // again before each cycle, not cached by sensor_core.
    uint32_t (*poll_interval_ms)(sensor_inst_t *inst);

    // Reads one configuration key's current value from the chip -- never
    // a cached copy. This is what makes SCFG's read-back verification
    // real. For an ENUM key, *out is an index into cfgkeys[i].enum_values.
    esp_err_t (*cfg_get)(sensor_inst_t *inst, const sensor_cfgkey_def_t *key, int32_t *out);

    // Writes one configuration key to the chip. Ordering rules between
    // related keys (see docs/09-sensor-configuration.md) are the driver's
    // own responsibility -- sensor_core calls cfg_set for one key at a
    // time, in no particular order, and never assumes a driver's internal
    // register layout.
    esp_err_t (*cfg_set)(sensor_inst_t *inst, const sensor_cfgkey_def_t *key, int32_t value);

    // Runs a named action (currently just calibration) with one integer
    // argument, and reports one integer result. NULL if this driver
    // defines no actions. action_name is the part of CAL's key argument
    // after the instance prefix, e.g. "offset" from "tof0.offset".
    esp_err_t (*action)(sensor_inst_t *inst, const char *action_name, int32_t arg, int32_t *result);
} sensor_driver_t;

// What a sensor module needs to reach its hardware, supplied by whatever
// assembles the device (main/devices/<device>/app_main.c), which gets it
// from the board component. sensor_core does not interpret this -- it is
// handed to the driver's probe()/init() unchanged.
typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    uint16_t i2c_addr;

    // Shared across every sensor instance on this bus (board_i2c_bus_init()
    // returns the same bus and mutex to every caller). A driver whose own
    // register writes span more than one I2C transaction should hold this
    // for the whole sequence, not just one transaction inside it -- two
    // FreeRTOS tasks can run at the same instant on this chip's two CPU
    // cores. See AGENTS.md, "Firmware mistakes to avoid".
    SemaphoreHandle_t i2c_bus_mutex;
} sensor_board_binding_t;

// ---------------------------------------------------------------------------
// Registration and lifecycle
// ---------------------------------------------------------------------------

// Registers device_proto's SENS, SCAPS, SCFG, and CAL verbs, registers the
// combined save/load persistence hook with device_config, and starts the
// poll task. Call once, before any sensor_core_register() call, and after
// device_config_init() and proto_cmd_init().
esp_err_t sensor_core_init(void);

// Registers one sensor instance: probes it, and on success initializes it,
// applies its persisted-or-default configuration, and starts it. Always
// registers the instance's channels and configuration keys on the wire,
// whether or not the probe succeeded -- see sensor_driver_t::probe above.
//
// @param[in] drv The driver's static definition. Not copied; must outlive
//     the instance (a driver's own file-scope const is the normal case).
// @param[in] instance_name A short name such as "tof0", unique among
//     registered instances. Copied.
// @param[in] binding How to reach the hardware. Copied.
// @param[out] out_inst The new instance handle, for the driver's own use
//     (e.g. to store a user-context pointer with sensor_core_set_user_ctx).
//     May be NULL if the caller does not need it.
// @return ESP_OK (even if probe() failed -- check sensor_core via a SENS
//     row for fault state), ESP_ERR_NO_MEM if full, or
//     ESP_ERR_INVALID_SIZE if the driver declares more than
//     SENSOR_CORE_MAX_CFGKEYS_PER_DRIVER configuration keys.
esp_err_t sensor_core_register(const sensor_driver_t *drv, const char *instance_name,
                               const sensor_board_binding_t *binding, sensor_inst_t **out_inst);

// A driver-private pointer, NULL until set. A driver stores its own
// per-instance state here (e.g. a cached sample struct) instead of using
// file-scope statics, so sensor_core's design supports more than one
// instance of the same driver type in the future, even though only one
// sensor is active on the AtomS3 Lite today.
void sensor_core_set_user_ctx(sensor_inst_t *inst, void *ctx);
void *sensor_core_get_user_ctx(sensor_inst_t *inst);

// Returns this instance's binding, as supplied to sensor_core_register().
const sensor_board_binding_t *sensor_core_get_binding(sensor_inst_t *inst);

// Returns this instance's name, as supplied to sensor_core_register().
const char *sensor_core_get_name(sensor_inst_t *inst);

// True if any registered instance's probe() failed, or has since been
// faulted with sensor_core_set_fault(). A device's app_main.c uses this to
// drive the FAULT status LED state; see docs/10-led-status.md.
bool sensor_core_any_fault(void);

// Lets a driver report that it no longer trusts its own data -- for
// example, after several consecutive poll() failures, or a configuration
// write that could not be confirmed. Demotes the instance to the same
// "fault" state a failed probe() produces: no further poll() calls, and
// GET/SCFG/CAL on this instance report null/ESTATE until a restart. See
// AGENTS.md, "Firmware mistakes to avoid" -- a driver must say when it
// stops trusting its data, rather than keep reporting a stale value as if
// it were live.
void sensor_core_set_fault(sensor_inst_t *inst);
