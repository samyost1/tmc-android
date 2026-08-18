#include "tmc_ra_state.h"

#include <string.h>

static const uint8_t sMagic[] = {'T', 'M', 'R', 'A'};
#define TMC_RA_STATE_VERSION 1u

static void WriteLe32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static uint32_t ReadLe32(const uint8_t* src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t Crc32(const uint8_t* bytes, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;

    for (i = 0; i < size; ++i) {
        unsigned bit;

        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & UINT32_C(1)) ? UINT32_C(0xedb88320) : UINT32_C(0));
    }
    return ~crc;
}

size_t TmcRaState_BlockSize(size_t payload_size) {
    if (payload_size == 0 || payload_size > TMC_RA_STATE_MAX_PAYLOAD)
        return 0;
    return TMC_RA_STATE_HEADER_SIZE + payload_size;
}

bool TmcRaState_Encode(uint8_t* block, size_t block_size, const uint8_t* payload, size_t payload_size) {
    const size_t expected_size = TmcRaState_BlockSize(payload_size);

    if (block == NULL || payload == NULL || expected_size == 0 || block_size != expected_size)
        return false;
    memcpy(block, sMagic, sizeof(sMagic));
    WriteLe32(block + 4, TMC_RA_STATE_VERSION);
    WriteLe32(block + 8, 0);
    WriteLe32(block + 12, (uint32_t)payload_size);
    WriteLe32(block + 16, Crc32(payload, payload_size));
    memcpy(block + TMC_RA_STATE_HEADER_SIZE, payload, payload_size);
    return true;
}

TmcRaStateDecodeResult TmcRaState_Decode(const uint8_t* block, size_t block_size, const uint8_t** payload,
                                         size_t* payload_size) {
    size_t size;
    const uint8_t* data;

    if (payload == NULL || payload_size == NULL)
        return TMC_RA_STATE_INVALID;
    *payload = NULL;
    *payload_size = 0;
    if (block_size == 0)
        return TMC_RA_STATE_EMPTY;
    if (block == NULL || block_size < TMC_RA_STATE_HEADER_SIZE)
        return TMC_RA_STATE_INVALID;
    if (memcmp(block, sMagic, sizeof(sMagic)) != 0 || ReadLe32(block + 4) != TMC_RA_STATE_VERSION ||
        ReadLe32(block + 8) != 0)
        return TMC_RA_STATE_INVALID;
    size = ReadLe32(block + 12);
    if (TmcRaState_BlockSize(size) != block_size)
        return TMC_RA_STATE_INVALID;
    data = block + TMC_RA_STATE_HEADER_SIZE;
    if (ReadLe32(block + 16) != Crc32(data, size))
        return TMC_RA_STATE_INVALID;
    *payload = data;
    *payload_size = size;
    return TMC_RA_STATE_VALID;
}
