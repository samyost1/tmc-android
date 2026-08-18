#include "tmc_ra_adapter.h"

#include "port_rom.h"
#include "tmc_ra_casual_attestation.generated.h"
#include "tmc_ra_memory.h"
#include "tmc_ra_policy.h"

#include "rc_consoles.h"
#include "rc_hash.h"

#include <assert.h>
#include <string.h>

static bool tmc_ra_attestation_ranges_valid(void) {
    uint64_t selected = 0;
    uint32_t previous_end = 0;
    size_t i;

    if (TMC_RA_CASUAL_ATTESTATION_VERSION != 1u ||
        TMC_RA_CASUAL_SNAPSHOT_BYTES != TMC_RA_SNAPSHOT_BYTES ||
        TMC_RA_CASUAL_RANGE_COUNT !=
            sizeof(kTmcRaCasualRanges) / sizeof(kTmcRaCasualRanges[0]) ||
        TMC_RA_CASUAL_RANGE_COUNT == 0)
        return false;

    for (i = 0; i < TMC_RA_CASUAL_RANGE_COUNT; ++i) {
        const TmcRaCasualRange range = kTmcRaCasualRanges[i];
        if (range.size == 0 || range.address < previous_end ||
            range.address > TMC_RA_SNAPSHOT_BYTES ||
            range.size > TMC_RA_SNAPSHOT_BYTES - range.address)
            return false;
        previous_end = range.address + range.size;
        selected += range.size;
    }
    return selected == TMC_RA_CASUAL_SELECTED_BYTES;
}

static bool tmc_ra_verify_attestation(TmcRaAdapter* adapter) {
    char rom_md5[33];

    if (adapter == NULL)
        return false;
    if (adapter->attestation_checked)
        return adapter->memory_fully_validated;

    adapter->attestation_checked = true;
    adapter->memory_fully_validated = false;
    if (gRomData == NULL || gRomSize == 0 ||
        TMC_RA_CASUAL_GAME_ID != 559u ||
        TMC_RA_CASUAL_CONSOLE_ID != RC_CONSOLE_GAMEBOY_ADVANCE ||
        strcmp(TMC_RA_CASUAL_MEMORY_MANIFEST_SHA256, TmcRaMemory_ManifestHash()) != 0 ||
        !tmc_ra_attestation_ranges_valid() ||
        !rc_hash_generate_from_buffer(rom_md5, RC_CONSOLE_GAMEBOY_ADVANCE,
                                      gRomData, gRomSize) ||
        strcmp(rom_md5, TMC_RA_CASUAL_ROM_MD5) != 0)
        return false;

    adapter->memory_fully_validated = true;
    return true;
}

static bool tmc_ra_range_attested(uint32_t address, uint32_t count) {
    size_t i;

    if (count == 0 || address > TMC_RA_SNAPSHOT_BYTES ||
        count > TMC_RA_SNAPSHOT_BYTES - address)
        return false;
    for (i = 0; i < TMC_RA_CASUAL_RANGE_COUNT; ++i) {
        const TmcRaCasualRange range = kTmcRaCasualRanges[i];
        if (address < range.address)
            return false;
        if (count <= range.size &&
            address - range.address <= range.size - count)
            return true;
    }
    return false;
}

static NRA_Result tmc_ra_get_rom(void* userdata, NRA_RomView* rom) {
    (void)userdata;
    if (rom == NULL || gRomData == NULL || gRomSize == 0)
        return NRA_DISABLED;
    rom->path_hint = Port_GetLoadedRomPath();
    rom->data = gRomData;
    rom->size = gRomSize;
    rom->console_id = RC_CONSOLE_GAMEBOY_ADVANCE;
    return NRA_OK;
}

static NRA_Result tmc_ra_build_memory_snapshot(void* userdata, NRA_MemoryView* memory) {
    TmcRaSnapshotView snapshot;
    TmcRaAdapter* adapter = userdata;
    if (memory == NULL)
        return NRA_INVALID_ARGUMENT;

    snapshot = TmcRaMemory_Current();
    memory->data = snapshot.bytes;
    memory->size = TMC_RA_SNAPSHOT_BYTES;
    memory->generation = snapshot.generation;
    /* Live remains gated until every address in this canonical view is proven. */
#ifdef TMC_RA_RUNTIME_TEST
    memory->fully_validated = adapter != NULL && adapter->memory_fully_validated;
#else
    memory->fully_validated = tmc_ra_verify_attestation(adapter);
#endif
    if (adapter != NULL)
        adapter->memory_fully_validated = memory->fully_validated;
    return NRA_OK;
}

static bool tmc_ra_read_memory_snapshot(void* userdata, uint32_t address, uint8_t* buffer, uint32_t count) {
    TmcRaAdapter* adapter = userdata;
    if (!tmc_ra_verify_attestation(adapter) ||
        !tmc_ra_range_attested(address, count)) {
        if (buffer != NULL)
            memset(buffer, 0, count);
        return false;
    }
    return TmcRaMemory_ReadBlock(address, buffer, count, count);
}

static void tmc_ra_release_memory_snapshot(void* userdata, const NRA_MemoryView* memory) {
    (void)userdata;
    assert(memory == NULL || (memory->data != NULL && memory->size == TMC_RA_SNAPSHOT_BYTES));
}

static void tmc_ra_request_full_reset(void* userdata, uint32_t reason) {
    TmcRaAdapter* adapter = userdata;
    (void)reason;
    if (adapter != NULL)
        adapter->reset_requested = true;
}

static bool tmc_ra_admit_mode(void* userdata, NRA_Mode requested_mode, bool game_loaded) {
    TmcRaAdapter* adapter = userdata;
    bool memory_fully_validated;

    if (requested_mode != NRA_MODE_LIVE_CASUAL)
        return false;
#ifdef TMC_RA_RUNTIME_TEST
    memory_fully_validated = adapter != NULL && adapter->memory_fully_validated;
#else
    memory_fully_validated = tmc_ra_verify_attestation(adapter);
#endif
    if (!game_loaded && memory_fully_validated)
        return true;
    return TmcRaPolicy_CanAdmitMode(requested_mode, game_loaded, memory_fully_validated);
}

static void tmc_ra_apply_capability_policy(void* userdata, const NRA_CapabilityPolicy* policy) {
    TmcRaAdapter* adapter = userdata;
    if (adapter != NULL)
        adapter->strict_mode = policy != NULL && policy->strict_mode;
}

static bool tmc_ra_accept_identified_game(void* userdata, uint32_t game_id, uint32_t console_id) {
    TmcRaAdapter* adapter = userdata;
    const bool accepted = game_id == TMC_RA_CASUAL_GAME_ID &&
                          console_id == TMC_RA_CASUAL_CONSOLE_ID;

    if (adapter != NULL) {
        adapter->admission = accepted ? TMC_RA_ADMISSION_SUPPORTED : TMC_RA_ADMISSION_UNSUPPORTED;
        adapter->identified_game_id = game_id;
    }
    return accepted;
}

void TmcRaAdapter_Init(TmcRaAdapter* adapter) {
    if (adapter != NULL) {
        TmcRaMemory_ResetRequestedCoverage();
        memset(adapter, 0, sizeof(*adapter));
    }
}

const NRA_GameAdapterVTable* TmcRaAdapter_VTable(void) {
    static const NRA_GameAdapterVTable vtable = {
        .get_rom = tmc_ra_get_rom,
        .build_memory_snapshot = tmc_ra_build_memory_snapshot,
        .read_memory_snapshot = tmc_ra_read_memory_snapshot,
        .release_memory_snapshot = tmc_ra_release_memory_snapshot,
        .request_full_reset = tmc_ra_request_full_reset,
        .admit_mode = tmc_ra_admit_mode,
        .apply_capability_policy = tmc_ra_apply_capability_policy,
        .accept_identified_game = tmc_ra_accept_identified_game,
    };
    return &vtable;
}

TmcRaAdmission TmcRaAdapter_Admission(const TmcRaAdapter* adapter) {
    return adapter ? adapter->admission : TMC_RA_ADMISSION_UNKNOWN;
}

bool TmcRaAdapter_TakeResetRequest(TmcRaAdapter* adapter) {
    const bool requested = adapter != NULL && adapter->reset_requested;
    if (adapter != NULL)
        adapter->reset_requested = false;
    return requested;
}

bool TmcRaAdapter_StrictMode(const TmcRaAdapter* adapter) {
    return adapter != NULL && adapter->strict_mode;
}

bool TmcRaAdapter_MemoryFullyValidated(const TmcRaAdapter* adapter) {
    return adapter != NULL && adapter->memory_fully_validated;
}
