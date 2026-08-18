#ifndef TMC_RA_STATE_H
#define TMC_RA_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMC_RA_STATE_HEADER_SIZE 20u
#define TMC_RA_STATE_MAX_PAYLOAD (1024u * 1024u)

/*
 * Wire format, with every multi-byte field stored little-endian:
 * magic[4], version[4], reserved[4], payload_size[4], crc32(payload)[4],
 * followed by payload_size bytes.
 */
typedef enum TmcRaStateDecodeResult {
    TMC_RA_STATE_EMPTY = 0,
    TMC_RA_STATE_VALID = 1,
    TMC_RA_STATE_INVALID = 2,
} TmcRaStateDecodeResult;

size_t TmcRaState_BlockSize(size_t payload_size);
bool TmcRaState_Encode(uint8_t* block, size_t block_size, const uint8_t* payload, size_t payload_size);
TmcRaStateDecodeResult TmcRaState_Decode(const uint8_t* block, size_t block_size, const uint8_t** payload,
                                         size_t* payload_size);

#ifdef __cplusplus
}
#endif

#endif
