#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Sets up CDC0 (the protocol port) and starts the RX task. CDC1 (the log
// port), the log ring buffer, and ESP_LOG routing are only set up when
// enable_debug_port is true -- normally false, so only one port exists on
// the bus. See docs/04-usb-device-and-descriptors.md and
// docs/12-flow-control-and-backpressure.md.
//
// @param[in] port_label Used to build the CDC0 and CDC1 interface name
//     strings: "<port_label> Data" and "<port_label> Debug". A device's
//     app_main.c supplies this; device_proto has no product name of its
//     own. See docs/16-adding-a-device.md.
// @param[in] enable_debug_port Whether to also bring up CDC1.
esp_err_t usb_cdc_init(const char *port_label, bool enable_debug_port);

// Sends one command response frame on CDC0. Used for a synchronous reply to
// a command the host is waiting for.
//
// @param[in] cbor_payload The frame's CBOR array, already encoded.
// @param[in] len Number of bytes in cbor_payload.
// @return true if the frame was queued and flushed.
bool usb_cdc_send_response_frame(const uint8_t *cbor_payload, size_t len);

// Sends one sample frame on CDC0, without blocking. If the transmit buffer
// is full, the sample is dropped, the samples_dropped counter goes up, and
// the DROP bit is set on the next frame that does send successfully. See
// docs/12-flow-control-and-backpressure.md, Rule 5.
//
// @param[in] cbor_payload The frame's CBOR array, already encoded, with its
//     flags item still at the value returned by usb_cdc_take_pending_drop().
// @param[in] len Number of bytes in cbor_payload.
void usb_cdc_send_sample_frame(const uint8_t *cbor_payload, size_t len);

// Returns true, and clears the pending state, if a sample was dropped since
// the last call. The caller sets the DROP bit for the sample frame it is
// about to build.
bool usb_cdc_take_pending_drop(void);

// Counts one frame that could not be trusted enough to answer: a COBS or
// CRC failure, or a CBOR array too broken to hold a sequence number. See
// docs/11-error-codes.md.
void usb_cdc_note_crc_error(void);

// The device's USB serial number string, built from the eFuse MAC. See
// docs/04-usb-device-and-descriptors.md.
const char *usb_cdc_get_serial(void);

typedef struct {
    uint32_t frames_rx;
    uint32_t frames_tx;
    uint32_t crc_errors;
    uint32_t samples_dropped;
    uint32_t log_lines_dropped;
} usb_cdc_stats_t;

void usb_cdc_get_stats(usb_cdc_stats_t *out);
