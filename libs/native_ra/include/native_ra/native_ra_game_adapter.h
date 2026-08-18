#ifndef NATIVE_RA_GAME_ADAPTER_H
#define NATIVE_RA_GAME_ADAPTER_H

#include "native_ra_types.h"

typedef struct NRA_RomView {
    const char* path_hint;
    const uint8_t* data;
    size_t size;
    uint32_t console_id;
} NRA_RomView;

typedef struct NRA_MemoryView {
    const uint8_t* data;
    size_t size;
    uint64_t generation;
    bool fully_validated;
} NRA_MemoryView;

typedef struct NRA_GameAdapterVTable {
    NRA_Result (*get_rom)(void* userdata, NRA_RomView* rom);
    NRA_Result (*build_memory_snapshot)(void* userdata, NRA_MemoryView* memory);
    /*
     * Reads from the snapshot most recently returned by build_memory_snapshot.
     * Return true only after filling every requested byte. The core checks
     * bounds first; false makes rcheevos treat the entire read as invalid.
     * A partial-validity snapshot requires this callback. Direct memory.data
     * copying is reserved for explicitly fully_validated views.
     */
    bool (*read_memory_snapshot)(void* userdata, uint32_t address, uint8_t* buffer, uint32_t count);
    void (*release_memory_snapshot)(void* userdata, const NRA_MemoryView* memory);
    void (*request_full_reset)(void* userdata, uint32_t reason);
    bool (*admit_mode)(void* userdata, NRA_Mode requested_mode, bool game_loaded);
    void (*apply_capability_policy)(void* userdata, const NRA_CapabilityPolicy* policy);
    bool (*is_game_tick_processable)(void* userdata);
    bool (*accept_identified_game)(void* userdata, uint32_t game_id, uint32_t console_id);
    void (*on_ra_game_loaded)(void* userdata, uint32_t game_id);
    void (*on_ra_game_unloaded)(void* userdata);
} NRA_GameAdapterVTable;

#endif
