#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Turn a CBOR payload into a wire frame.
 *
 * Adds a CRC, encodes with COBS, and adds the trailing 0x00 delimiter.
 * See docs/06-cbor-encoding.md.
 *
 * @param[in] payload The CBOR-encoded frame content.
 * @param[in] payload_len Number of bytes in payload.
 * @param[out] out Buffer to receive the wire frame, including the delimiter.
 * @param[in] out_cap Size of out, in bytes.
 * @param[out] out_len Number of bytes written to out.
 * @return true on success. false if out_cap is too small.
 */
bool proto_frame_encode(const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * @brief Recover a CBOR payload from one COBS-encoded wire frame.
 *
 * @param[in] wire The COBS-encoded bytes, without the trailing 0x00 delimiter.
 * @param[in] wire_len Number of bytes in wire.
 * @param[out] out Buffer to receive the CBOR payload.
 * @param[in] out_cap Size of out, in bytes.
 * @param[out] out_len Number of bytes written to out.
 * @return true if COBS decoding and the CRC check both succeed.
 */
bool proto_frame_decode(const uint8_t *wire, size_t wire_len, uint8_t *out, size_t out_cap, size_t *out_len);
