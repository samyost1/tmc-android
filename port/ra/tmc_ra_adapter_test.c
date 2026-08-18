/*
 * Reuse the hermetic memory bridge fixture so this adapter test exercises
 * actual validity metadata without a ROM, save file, SDL, or game loop.
 */
#define main tmc_ra_memory_test_main
#include "tmc_ra_memory_test.c"
#undef main

#include "tmc_ra_adapter.h"
#include "tmc_ra_casual_attestation.generated.h"

#include "rc_hash.h"

#include <stdio.h>
#include <string.h>

const char* Port_GetLoadedRomPath(void) {
    return "synthetic.gba";
}

int RC_CCONV rc_hash_generate_from_buffer(char hash[33], uint32_t console_id,
                                          const uint8_t* buffer, size_t buffer_size) {
    const bool canonical_fixture =
        console_id == TMC_RA_CASUAL_CONSOLE_ID && buffer != NULL &&
        buffer_size == 4 && buffer[0] == 0x12;
    memcpy(hash, canonical_fixture ? TMC_RA_CASUAL_ROM_MD5 :
                                     "00000000000000000000000000000000",
           33);
    return 1;
}

static int adapter_fails;
#define CHECK_ADAPTER(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", #x); ++adapter_fails; } } while (0)

int main(void) {
    const NRA_GameAdapterVTable* vtable = TmcRaAdapter_VTable();
    TmcRaAdapter adapter;
    NRA_RomView rom = {0};
    NRA_MemoryView memory = {0};
    TmcRaReadAudit audit;
    TmcRaRequestedCoverage coverage;
    uint8_t synthetic_rom[] = { 0x12, 0x34, 0x56, 0x78 };
    uint8_t bytes[2] = { 0xaa, 0xbb };
    uint64_t generation;

    if (tmc_ra_memory_test_main() != 0)
        return 1;
    gRomData = synthetic_rom;
    gRomSize = sizeof(synthetic_rom);
    TmcRaAdapter_Init(&adapter);
    CHECK_ADAPTER(vtable->get_rom(&adapter, &rom) == NRA_OK);
    CHECK_ADAPTER(rom.data == synthetic_rom && rom.size == sizeof(synthetic_rom));
    CHECK_ADAPTER(rom.path_hint != NULL && rom.console_id == 5);

    generation = TmcRaMemory_Current().generation;
    CHECK_ADAPTER(vtable->build_memory_snapshot(&adapter, &memory) == NRA_OK);
    CHECK_ADAPTER(memory.data == TmcRaMemory_Current().bytes && memory.size == TMC_RA_SNAPSHOT_BYTES);
    CHECK_ADAPTER(memory.fully_validated);
    /* The owner-thread runtime publishes once before entering the native
     * core; adapter snapshot construction is a value-only view boundary. */
    CHECK_ADAPTER(memory.generation == generation);
    CHECK_ADAPTER(TmcRaMemory_Current().generation == memory.generation);
    CHECK_ADAPTER(vtable->admit_mode != NULL);
    CHECK_ADAPTER(vtable->admit_mode(&adapter, NRA_MODE_LIVE_CASUAL, false));
    CHECK_ADAPTER(!vtable->admit_mode(&adapter, NRA_MODE_SPECTATOR, false));
    adapter.memory_fully_validated = true;
    CHECK_ADAPTER(vtable->admit_mode(&adapter, NRA_MODE_LIVE_CASUAL, false));
    CHECK_ADAPTER(vtable->admit_mode(&adapter, NRA_MODE_LIVE_CASUAL, true));

    TmcRaMemory_OverlayExplicit(0x0010, (const uint8_t[]){ 0x9a, 0xbc }, 2);
    TmcRaMemory_ResetAudit();
    TmcRaMemory_ResetRequestedCoverage();
    CHECK_ADAPTER(vtable->read_memory_snapshot(&adapter, 0x0010, bytes, 2));
    CHECK_ADAPTER(bytes[0] == 0x9a && bytes[1] == 0xbc);
    CHECK_ADAPTER(vtable->read_memory_snapshot(&adapter, 0x0ab0e, bytes, 1));
    CHECK_ADAPTER(vtable->read_memory_snapshot(&adapter, 0x0ab1f, bytes, 1));
    bytes[0] = 0xaa;
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x0ab0a, bytes, 1));
    CHECK_ADAPTER(bytes[0] == 0);
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x00fdc, bytes, 1));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x00ff7, bytes, 1));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x01000, bytes, 1));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x0100c, bytes, 1));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x00fdb, bytes, 2));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, TMC_RA_SNAPSHOT_BYTES, bytes, 1));
    audit = TmcRaMemory_ReadAudit();
    CHECK_ADAPTER(audit.requested_ranges == 3 && audit.requested_bytes == 4);
    CHECK_ADAPTER(audit.invalid_requested_ranges == 0 && audit.invalid_requested_bytes == 0);
    coverage = TmcRaMemory_RequestedCoverage();
    CHECK_ADAPTER(coverage.selected_bytes == 4 && coverage.out_of_range_bytes == 0);
    CHECK_ADAPTER(coverage.bitmap[0x0010] && coverage.bitmap[0x0011] &&
                  coverage.bitmap[0x0ab0e] && coverage.bitmap[0x0ab1f]);
    CHECK_ADAPTER(!coverage.bitmap[0x0ab0a] &&
                  !coverage.bitmap[0x00fdc] && !coverage.bitmap[0x00fdb] &&
                  !coverage.bitmap[0x01000] &&
                  !coverage.bitmap[0x00ff7] && !coverage.bitmap[0x0100c]);

    synthetic_rom[0] = 0;
    TmcRaAdapter_Init(&adapter);
    CHECK_ADAPTER(vtable->build_memory_snapshot(&adapter, &memory) == NRA_OK);
    CHECK_ADAPTER(!memory.fully_validated);
    CHECK_ADAPTER(!vtable->admit_mode(&adapter, NRA_MODE_LIVE_CASUAL, false));
    CHECK_ADAPTER(!vtable->read_memory_snapshot(&adapter, 0x0010, bytes, 1));
    synthetic_rom[0] = 0x12;
    TmcRaAdapter_Init(&adapter);
    coverage = TmcRaMemory_RequestedCoverage();
    CHECK_ADAPTER(coverage.selected_bytes == 0 && coverage.out_of_range_bytes == 0);

    CHECK_ADAPTER(vtable->accept_identified_game(&adapter, 559, 5));
    CHECK_ADAPTER(TmcRaAdapter_Admission(&adapter) == TMC_RA_ADMISSION_SUPPORTED);
    CHECK_ADAPTER(!vtable->accept_identified_game(&adapter, 558, 5));
    CHECK_ADAPTER(TmcRaAdapter_Admission(&adapter) == TMC_RA_ADMISSION_UNSUPPORTED);
    CHECK_ADAPTER(!vtable->accept_identified_game(&adapter, 559, 4));
    CHECK_ADAPTER(TmcRaAdapter_Admission(&adapter) == TMC_RA_ADMISSION_UNSUPPORTED);
    CHECK_ADAPTER(vtable->apply_capability_policy != NULL);
    vtable->apply_capability_policy(&adapter, &(NRA_CapabilityPolicy){.strict_mode = true});
    CHECK_ADAPTER(TmcRaAdapter_StrictMode(&adapter));
    vtable->apply_capability_policy(&adapter, &(NRA_CapabilityPolicy){0});
    CHECK_ADAPTER(!TmcRaAdapter_StrictMode(&adapter));
    vtable->request_full_reset(&adapter, 1);
    CHECK_ADAPTER(TmcRaAdapter_TakeResetRequest(&adapter));
    CHECK_ADAPTER(!TmcRaAdapter_TakeResetRequest(&adapter));
    vtable->release_memory_snapshot(&adapter, &memory);
    if (adapter_fails)
        return 1;
    puts("tmc_ra_adapter_test: ALL PASS");
    return 0;
}
