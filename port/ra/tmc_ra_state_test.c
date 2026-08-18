#include "tmc_ra_state.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void SetLe32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void AssertInvalid(const uint8_t* block, size_t block_size) {
    const uint8_t* payload = (const uint8_t*)(uintptr_t)1;
    size_t payload_size = 1;

    assert(TmcRaState_Decode(block, block_size, &payload, &payload_size) == TMC_RA_STATE_INVALID);
    assert(payload == NULL);
    assert(payload_size == 0);
}

int main(void) {
    const uint8_t expected[] = {0x21, 0x43, 0x65, 0x87};
    uint8_t block[TMC_RA_STATE_HEADER_SIZE + sizeof(expected)] = {0};
    uint8_t mutated[sizeof(block)];
    uint8_t oversized[sizeof(block) + 1];
    uint8_t oversized_payload[TMC_RA_STATE_HEADER_SIZE];
    const uint8_t* decoded = (const uint8_t*)(uintptr_t)1;
    size_t decoded_size = 1;

    assert(TmcRaState_BlockSize(0) == 0);
    assert(TmcRaState_BlockSize(sizeof(expected)) == sizeof(block));
    assert(TmcRaState_BlockSize(TMC_RA_STATE_MAX_PAYLOAD) ==
           TMC_RA_STATE_HEADER_SIZE + TMC_RA_STATE_MAX_PAYLOAD);
    assert(TmcRaState_BlockSize(TMC_RA_STATE_MAX_PAYLOAD + 1) == 0);

    assert(!TmcRaState_Encode(NULL, sizeof(block), expected, sizeof(expected)));
    assert(!TmcRaState_Encode(block, sizeof(block), NULL, sizeof(expected)));
    assert(!TmcRaState_Encode(block, sizeof(block), expected, 0));
    assert(!TmcRaState_Encode(block, sizeof(block) - 1, expected, sizeof(expected)));
    assert(!TmcRaState_Encode(block, sizeof(block) + 1, expected, sizeof(expected)));
    assert(!TmcRaState_Encode(block, sizeof(block), expected, TMC_RA_STATE_MAX_PAYLOAD + 1));

    assert(TmcRaState_Encode(block, sizeof(block), expected, sizeof(expected)));
    assert(memcmp(block, "TMRA", 4) == 0);
    assert(block[4] == 1 && block[5] == 0 && block[6] == 0 && block[7] == 0);
    assert(block[8] == 0 && block[9] == 0 && block[10] == 0 && block[11] == 0);
    assert(block[12] == sizeof(expected) && block[13] == 0 && block[14] == 0 && block[15] == 0);
    /* Independent CRC-32/ISO-HDLC vector for 21 43 65 87: 0x2040adbf. */
    assert(block[16] == 0xbf && block[17] == 0xad && block[18] == 0x40 && block[19] == 0x20);
    assert(TmcRaState_Decode(block, sizeof(block), &decoded, &decoded_size) == TMC_RA_STATE_VALID);
    assert(decoded == block + TMC_RA_STATE_HEADER_SIZE);
    assert(decoded_size == sizeof(expected));
    assert(memcmp(decoded, expected, sizeof(expected)) == 0);

    decoded = (const uint8_t*)(uintptr_t)1;
    decoded_size = 1;
    assert(TmcRaState_Decode(NULL, 0, &decoded, &decoded_size) == TMC_RA_STATE_EMPTY);
    assert(decoded == NULL && decoded_size == 0);
    assert(TmcRaState_Decode(block, 0, &decoded, &decoded_size) == TMC_RA_STATE_EMPTY);
    assert(TmcRaState_Decode(block, sizeof(block), NULL, &decoded_size) == TMC_RA_STATE_INVALID);
    assert(TmcRaState_Decode(block, sizeof(block), &decoded, NULL) == TMC_RA_STATE_INVALID);

    AssertInvalid(NULL, 1);
    AssertInvalid(block, TMC_RA_STATE_HEADER_SIZE - 1);
    AssertInvalid(block, sizeof(block) - 1);
    memset(oversized, 0, sizeof(oversized));
    memcpy(oversized, block, sizeof(block));
    AssertInvalid(oversized, sizeof(oversized));

    memcpy(mutated, block, sizeof(mutated));
    mutated[0] ^= 1;
    AssertInvalid(mutated, sizeof(mutated));
    memcpy(mutated, block, sizeof(mutated));
    mutated[4] = 2;
    AssertInvalid(mutated, sizeof(mutated));
    memcpy(mutated, block, sizeof(mutated));
    mutated[8] = 1;
    AssertInvalid(mutated, sizeof(mutated));
    memcpy(mutated, block, sizeof(mutated));
    mutated[16] ^= 1;
    AssertInvalid(mutated, sizeof(mutated));
    memcpy(mutated, block, sizeof(mutated));
    mutated[TMC_RA_STATE_HEADER_SIZE] ^= 1;
    AssertInvalid(mutated, sizeof(mutated));

    memcpy(oversized_payload, block, sizeof(oversized_payload));
    SetLe32(oversized_payload + 12, TMC_RA_STATE_MAX_PAYLOAD + 1);
    AssertInvalid(oversized_payload, sizeof(oversized_payload));

    puts("tmc_ra_state_test: ALL PASS");
    return 0;
}
