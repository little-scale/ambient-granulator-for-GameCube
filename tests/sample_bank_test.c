#include "sample_bank.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BANK_SIZE 4164
#define TEST_DATA_OFFSET 4160

static void write_u32_le(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
    bytes[offset + 2] = (uint8_t)(value >> 16);
    bytes[offset + 3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t index = 0; index < length; index++) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1)
                ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static void make_bank(uint8_t *bytes)
{
    memset(bytes, 0, TEST_BANK_SIZE);
    memcpy(bytes, "NDSGRN01", 8);
    write_u32_le(bytes, 8, 1);
    write_u32_le(bytes, 12, TEST_BANK_SIZE);
    write_u32_le(bytes, 16, 1);
    write_u32_le(bytes, 20, TEST_BANK_SIZE);
    write_u32_le(bytes, 24, 48000);
    write_u32_le(bytes, 28, 64);
    write_u32_le(bytes, 32, 64);
    write_u32_le(bytes, 36, TEST_DATA_OFFSET);
    memcpy(bytes + 64, "ENDIAN TEST", 11);
    write_u32_le(bytes, 96, TEST_DATA_OFFSET);
    write_u32_le(bytes, 100, 4);
    bytes[TEST_DATA_OFFSET] = 0x34;
    bytes[TEST_DATA_OFFSET + 1] = 0x12;
    bytes[TEST_DATA_OFFSET + 2] = 0xCC;
    bytes[TEST_DATA_OFFSET + 3] = 0xED;
    write_u32_le(bytes, 104,
                 crc32_bytes(bytes + TEST_DATA_OFFSET, 4));
}

int main(void)
{
    uint8_t bytes[TEST_BANK_SIZE];
    make_bank(bytes);
    SampleBank bank;
    LoadedSample sample;
    assert(sample_bank_open_memory(&bank, bytes, sizeof(bytes)));
    assert(bank.sample_count == 1);
    assert(bank.sample_rate == 48000);
    assert(strcmp(bank.entries[0].name, "ENDIAN TEST") == 0);
    assert(sample_bank_load(&bank, 0, &sample));
    assert(sample.sample_count == 2);
    assert(sample.samples[0] == 0x1234);
    assert(sample.samples[1] == (int16_t)0xEDCC);
    assert(strcmp(sample.name, "ENDIAN TEST") == 0);
    loaded_sample_free(&sample);
    sample_bank_close(&bank);

    bytes[TEST_DATA_OFFSET] ^= 1;
    assert(sample_bank_open_memory(&bank, bytes, sizeof(bytes)));
    assert(!sample_bank_load(&bank, 0, &sample));
    sample_bank_close(&bank);
    puts("sample_bank_test: ok");
    return 0;
}
