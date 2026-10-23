// Unit tests for components/device_proto/src/crc16.c (CRC-16/CCITT-FALSE).
#include "tinytest.h"
#include "crc16.h"

TEST(standard_check_value)
{
    CHECK_EQ(crc16_ccitt_false((const uint8_t *)"123456789", 9), 0x29B1);
}

TEST(empty_input_returns_init_value)
{
    CHECK_EQ(crc16_ccitt_false(NULL, 0), 0xFFFF);
}

TEST(single_zero_byte)
{
    const uint8_t z = 0;
    CHECK_EQ(crc16_ccitt_false(&z, 1), 0xE1F0);
}

TEST(ping_payload_matches_docs_06)
{
    // docs/06-cbor-encoding.md worked example: CRC is 0x3157.
    const uint8_t ping[] = { 0x83, 0x00, 0x01, 0x64, 0x50, 0x49, 0x4E, 0x47 };
    CHECK_EQ(crc16_ccitt_false(ping, sizeof(ping)), 0x3157);
}

TEST(sample_payload_matches_docs_06)
{
    // docs/06-cbor-encoding.md worked example: CRC is 0xDFC3.
    const uint8_t s[] = { 0x86, 0x04, 0x0C, 0x1A, 0x00, 0x03, 0xBD, 0x08, 0x69, 0x74, 0x6F,
                          0x66, 0x30, 0x2E, 0x64, 0x69, 0x73, 0x74, 0x19, 0x05, 0xF3, 0x00 };
    CHECK_EQ(crc16_ccitt_false(s, sizeof(s)), 0xDFC3);
}

TEST(single_bit_flip_changes_crc)
{
    uint8_t d[32];
    for (int i = 0; i < 32; i++)
        d[i] = (uint8_t)(i * 5);
    uint16_t base = crc16_ccitt_false(d, sizeof(d));
    for (int byte = 0; byte < 32; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            d[byte] ^= (uint8_t)(1u << bit);
            CHECK(crc16_ccitt_false(d, sizeof(d)) != base);
            d[byte] ^= (uint8_t)(1u << bit);
        }
    }
}

int main(void)
{
    RUN_TEST(standard_check_value);
    RUN_TEST(empty_input_returns_init_value);
    RUN_TEST(single_zero_byte);
    RUN_TEST(ping_payload_matches_docs_06);
    RUN_TEST(sample_payload_matches_docs_06);
    RUN_TEST(single_bit_flip_changes_crc);
    return tinytest_summary();
}
