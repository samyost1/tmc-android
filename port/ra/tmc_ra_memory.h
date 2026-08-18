#ifndef TMC_RA_MEMORY_H
#define TMC_RA_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TMC_RA_SNAPSHOT_BYTES 0x58000u
#define TMC_RA_IWRAM_BYTES    0x08000u
#define TMC_RA_EWRAM_BYTES    0x40000u
#define TMC_RA_SAVE_OFFSET    0x48000u
#define TMC_RA_SAVE_BYTES     0x10000u

typedef enum {
    TMC_RA_PROVENANCE_INVALID = 0,
    TMC_RA_PROVENANCE_RAW_IWRAM,
    TMC_RA_PROVENANCE_RAW_EWRAM,
    TMC_RA_PROVENANCE_SAVE,
    TMC_RA_PROVENANCE_EXPLICIT,
} TmcRaProvenance;

typedef enum {
    TMC_RA_POINTER_NULL = 0,
    TMC_RA_POINTER_RAW_EWRAM,
    TMC_RA_POINTER_RAW_IWRAM,
    TMC_RA_POINTER_ROM,
    TMC_RA_POINTER_ENTITY_POOL,
    TMC_RA_POINTER_FIXED_GLOBAL,
    TMC_RA_POINTER_UNKNOWN,
} TmcRaPointerCategory;

typedef struct {
    uint32_t requested_bytes;
    uint32_t invalid_requested_bytes;
    uint32_t requested_ranges;
    uint32_t invalid_requested_ranges;
} TmcRaReadAudit;

typedef struct {
    const uint8_t* bitmap;
    uint32_t selected_bytes;
    uint32_t out_of_range_bytes;
} TmcRaRequestedCoverage;

typedef struct {
    const uint8_t* bytes;
    const uint8_t* provenance;
    const uint8_t* validated;
    uint32_t generation;
    bool save_validated;
} TmcRaSnapshotView;

typedef struct {
    TmcRaSnapshotView snapshot;
    TmcRaRequestedCoverage coverage;
    TmcRaReadAudit audit;
} TmcRaFrameView;

typedef struct {
    const void* host;
    uint32_t gba_virtual;
    uint32_t host_bytes;
    uint32_t gba_bytes;
    uint32_t host_stride;
    uint32_t gba_stride;
} TmcRaFixedGlobal;

void TmcRaMemory_Publish(void);
TmcRaSnapshotView TmcRaMemory_Current(void);
TmcRaFrameView TmcRaMemory_CurrentFrameView(void);
const char* TmcRaMemory_ManifestHash(void);
void TmcRaMemory_ResetAudit(void);
TmcRaReadAudit TmcRaMemory_ReadAudit(void);
void TmcRaMemory_ResetRequestedCoverage(void);
TmcRaRequestedCoverage TmcRaMemory_RequestedCoverage(void);
uint8_t TmcRaMemory_Read(uint32_t ra_physical, bool* valid);
bool TmcRaMemory_ReadBlock(uint32_t ra_physical, uint8_t* out, size_t out_capacity, size_t bytes);
bool TmcRaMemory_OverlayExplicit(uint32_t offset, const uint8_t* bytes, size_t bytes_count);

bool TmcRaMemory_VirtualToPhysical(uint32_t gba_virtual, uint32_t* ra_physical);
bool TmcRaMemory_WriteU8(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                         uint32_t offset, uint8_t value, TmcRaProvenance source);
bool TmcRaMemory_WriteU16Le(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                            uint32_t offset, uint16_t value, TmcRaProvenance source);
bool TmcRaMemory_WriteU32Le(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                            uint32_t offset, uint32_t value, TmcRaProvenance source);
bool TmcRaMemory_WriteTranslatedPointerLe(uint8_t* bytes, uint8_t* provenance, uint8_t* validated,
                                          size_t capacity, uint32_t offset, uint32_t gba_virtual,
                                          bool translated, TmcRaProvenance source);
bool TmcRaMemory_TranslateHostPointer(const void* pointer, const void* entity_pool, uint32_t entity_pool_bytes,
                                      uint32_t host_entity_stride, uint32_t gba_entity_stride,
                                      const TmcRaFixedGlobal* fixed_globals,
                                      size_t fixed_global_count, uint32_t* gba_virtual,
                                      TmcRaPointerCategory* category);

#ifdef TMC_RA_MEMORY_TEST
void TmcRaMemory_TestSetAudit(TmcRaReadAudit audit);
void TmcRaMemory_TestResetCoverageResetCount(void);
uint32_t TmcRaMemory_TestCoverageResetCount(void);
#endif

#endif
