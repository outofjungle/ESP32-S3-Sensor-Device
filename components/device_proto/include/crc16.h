#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Compute CRC-16/CCITT-FALSE over a byte buffer.
 *
 * Poly 0x1021, init 0xFFFF, no reflection, no final XOR.
 * See docs/06-cbor-encoding.md.
 *
 * @param[in] data Bytes to check.
 * @param[in] len Number of bytes in data.
 * @return The 16-bit CRC value.
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);
