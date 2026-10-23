#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Encode a buffer with Consistent Overhead Byte Stuffing (COBS).
 *
 * The result never contains a 0x00 byte. See docs/06-cbor-encoding.md.
 * This function does not add the trailing 0x00 frame delimiter. The caller
 * adds it after writing the encoded bytes.
 *
 * @param[in] data Bytes to encode.
 * @param[in] len Number of bytes in data.
 * @param[out] out Buffer to receive the encoded bytes.
 * @param[in] out_cap Size of out, in bytes.
 * @param[out] out_len Number of bytes written to out.
 * @return true on success. false if out_cap is too small.
 */
bool cobs_encode(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * @brief Decode a COBS-encoded buffer.
 *
 * @param[in] data COBS-encoded bytes, without the trailing 0x00 delimiter.
 * @param[in] len Number of bytes in data.
 * @param[out] out Buffer to receive the decoded bytes.
 * @param[in] out_cap Size of out, in bytes.
 * @param[out] out_len Number of bytes written to out.
 * @return true on success. false if the input is malformed or out_cap is too small.
 */
bool cobs_decode(const uint8_t *data, size_t len, uint8_t *out, size_t out_cap, size_t *out_len);
