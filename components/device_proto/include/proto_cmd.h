#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cbor.h"

typedef void (*proto_cmd_handler_t)(uint32_t seq, CborValue *it);

// Registers device_proto's own built-in verbs (PING, ID, CAPS, STAT, RST,
// ECHO, GET, SET, CFG, SUB, UNSUB, START, STOP, SAVE, LOAD) and records the
// device name reported by ID. Call once, before usb_cdc_init(), so a
// command arriving the instant the port opens has a verb table to dispatch
// into. See docs/07-command-reference.md.
//
// @param[in] device_name Reported by ID. A device's app_main.c supplies
//     this; device_proto has no product name of its own.
void proto_cmd_init(const char *device_name);

// Registers a callback run once after each GET command that reaches a
// channel (single-channel or "*"), whether or not the read succeeded.
// status_led.c uses this for its one-shot flash; see docs/10-led-status.md.
// device_proto does not know what a flash is -- this is the same narrow,
// one-direction channel as device_config's LED_MODE hook.
void proto_cmd_set_on_get_hook(void (*hook)(void));

// Registers one additional verb's handler. sensor_core uses this to add
// SENS, SCAPS, SCFG, and CAL (see docs/09-sensor-configuration.md), without
// device_proto needing to know sensor_core exists -- device_proto only
// knows that some verb name maps to some handler.
//
// @return ESP_OK, or ESP_ERR_NO_MEM if the verb table is full, or
//     ESP_ERR_INVALID_STATE if the verb is already registered.
esp_err_t proto_cmd_register_verb(const char *verb, proto_cmd_handler_t handler);

/**
 * @brief Handle one command frame's CBOR payload.
 *
 * Parses [0, seq, verb, arg, ...], runs the command, and sends the response
 * frame or frames through usb_cdc_send_response_frame(). See
 * docs/05-message-model.md and docs/07-command-reference.md.
 *
 * @param[in] payload A CBOR payload already checked by proto_frame_decode().
 * @param[in] len Number of bytes in payload.
 */
void proto_cmd_dispatch(const uint8_t *payload, size_t len);
