/*
 * port_quicksave.c — multi-slot save-states with disk persistence + auto-save.
 *
 * Snapshots a curated set of game-state regions into an in-memory slot
 * on save, restores them on load. Slots also get serialized to disk as
 * `state_<slot>.bin` next to the binary so they survive restart.
 *
 * Slot layout:
 *   slot 0:    F5 quicksave / F6 quickload (legacy single-slot API)
 *   slot 1-4:  F1..F4 load,  Shift+F1..F4 save (numbered manual slots)
 *   slot 5-7:  auto-save ring (Port_QuickSave_Auto cycles through these)
 *
 * File format (disk persistence):
 *   magic    "TMCS"                          (4 bytes)
 *   version  PORT_QUICKSAVE_VERSION          (u32 LE)
 *   total    sum of all region sizes         (u32 LE)
 *   data     concatenated region bytes       (in sRegions[] order)
 *
 * On load, if magic/version/size don't match, the file is rejected
 * silently — the in-memory snapshot (if any) stays untouched. This is
 * defensive against schema changes between builds; saves are best-effort,
 * not a contract.
 *
 * Coverage: emulated GBA memory (EWRAM/IWRAM/VRAM/IO), the save file,
 * the player + state, the room controls + transition, gMain, and the
 * full gEntities array. Anything not in this list (HUD state, OAM, gfx
 * slots, palette buffers) will visually catch up over the next frame.
 *
 * Caveats:
 *  - Snapshotting mid-frame is supported but the visible result is
 *    "next frame" — entity logic that ran this frame may have already
 *    written to OAM, which is not snapshotted.
 *  - Save-states are not the same as the game's in-engine save file
 *    (`tmc.sav`). The game's own save still goes through its file-select
 *    flow. Save-states capture transient runtime state including
 *    mid-cutscene positions.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <SDL3/SDL.h>

#include "common.h"
#include "structures.h"
#include "save.h"
#include "main.h"
#include "game.h"
#include "entity.h"
#include "area.h"
#include "room.h"
#include "screen.h"
#include "fade.h"
#define gMapDataBottomSpecial gMapDataBottomSpecial_HIDDEN
#include "map.h"
#undef gMapDataBottomSpecial
#include "port_gba_mem.h"
#include "port_runtime_config.h"
#include "region.h" /* REGION_IS_EU/JP — per-region savestate isolation (#21) */

extern u8 gEwram[];
extern u8 gIwram[];
extern u8 gVram[];
extern u8 gIoMem[];
extern u16 gBgPltt[256];
extern u16 gObjPltt[256];
extern u16 gOamMem[512];
extern VBlankDMA gVBlankDMA;
extern u8 gUpdateVisibleTiles;

/* The gameplay PRNG seed. On GBA this lived in IWRAM (0x03001150) and was
 * therefore captured by an IWRAM-snapshotting savestate; in the port it is a
 * standalone host global (port_linked_stubs.c), so it must be listed explicitly
 * or QuickLoad would restore everything EXCEPT RNG state and desync manips. */
extern u32 gRand;

/* Speedrun-practice IGT frame counter (port_practice.c). Listed as a region
 * so loading any savestate rewinds the practice timer to the value captured
 * with that state — "reload the section and the timer comes back too". */
extern u64 gPracticeFrame;

/* Defined in src/player.c — re-resolves the player's .rodata hitbox pointer
   from the current form after a cross-process quickload (FixupEntityPointers
   only relocates pointers that land inside gEntities). */
void Port_RestorePlayerHitbox(void);
extern void Port_DebugAction_ArmWarpNudge(void);

typedef struct {
    void* ptr;
    size_t size;
    const char* name;
} StateRegion;

/* List of regions captured by a save-state. The order doesn't matter for
 * save, but for restore the order must stay consistent with what was on
 * disk — which is why the disk format records the total size and we
 * reject files that don't match the current region layout. */
static StateRegion sRegions[] = {
    { gEwram, 0x40000, "gEwram" },
    { gIwram, 0x8000, "gIwram" },
    { gVram, 0x18000, "gVram" },
    { gIoMem, 0x400, "gIoMem" },
    { &gSave, sizeof(gSave), "gSave" },
    { &gPlayerEntity, sizeof(gPlayerEntity), "gPlayerEntity" },
    { &gPlayerState, sizeof(gPlayerState), "gPlayerState" },
    { &gMain, sizeof(gMain), "gMain" },
    { &gRoomControls, sizeof(gRoomControls), "gRoomControls" },
    { &gRoomTransition, sizeof(gRoomTransition), "gRoomTransition" },
    { gEntities, sizeof(gEntities), "gEntities" },
    { &gRand, sizeof(gRand), "gRand" },
    { &gPracticeFrame, sizeof(gPracticeFrame), "gPracticeFrame" },
};

/* Auxiliary in-memory PPU & engine state captured alongside sRegions[] for
 * same-session in-process QuickSave/QuickLoad so palette buffers, fade state,
 * screen control, entity lists, and room vars round-trip without visual
 * glitches, while keeping the on-disk VERSION 6 format unchanged. */
typedef struct {
    u16 bgPltt[256];
    u16 objPltt[256];
    u8 oamMem[0x400];
    u16 paletteBuffer[0x200];
    u32 usedPalettes;
    Screen screen;
    FadeControl fadeControl;
    struct_020354C0 unk_020354C0[32];
    OAMControls oamControls;
    VBlankDMA vblankDma;
    u8 updateVisibleTiles;
    UI ui;
    HUD hud;
    GfxSlotList gfxSlots;
    Area area;
    RoomVars roomVars;
    LinkedList entityLists[9];
    u8 entCount;
    u8 managerCount;
    MapLayer mapBottom;
    MapLayer mapTop;
    RoomMemory roomMemory[8];
} SlotAuxState;

#define NUM_REGIONS (sizeof(sRegions) / sizeof(sRegions[0]))
#define NUM_SLOTS 9 /* 0..4 manual + 5..7 auto-save ring + 8 auto-resume */
#define AUTO_SLOT_BASE 5
#define NUM_AUTO_SLOTS 3
#define AUTO_RESUME_SLOT 8
#define MAGIC 0x53434D54u /* "TMCS" little-endian */
#define VERSION                                         \
    6u /* v2: header carries gEntities base address for \
        * cross-process pointer-fixup on restore.       \
        * v3: gRand added to region list so RNG         \
        * state round-trips (GBA had it in IWRAM).      \
        * v4: gPracticeFrame added so speedrun IGT      \
        * timer rewinds with state.                     \
        * v5: ROM region tag — USA state restored     \
        * into a JP session contaminates tmc_jp.sav     \
        * (#21); cross-region loads are refused.        \
        * v6: entity subclass layouts changed; older    \
        * snapshots are rejected. */

typedef struct {
    u8* snapshot; /* heap, NULL if slot empty */
    size_t bytes;
    int valid;
    u64 saved_at_unix;       /* clock_gettime CLOCK_REALTIME seconds */
    u64 saved_entities_base; /* gEntities address at save time; 0 for in-RAM slots */
    SlotAuxState* aux;       /* heap, populated for in-process captures */
} Slot;

static Slot sSlots[NUM_SLOTS];
static int sAutoNextSlot = AUTO_SLOT_BASE; /* round-robin cursor */
static u64 sAutoLastIntervalTicksMs = 0;
static u64 sAutoLastAreaChangeTicksMs = 0;
static int sAutoEnabled = 1;        /* on by default — the F8
                                       toggle (and config.json)
                                       can flip it off. */
static u32 sAutoIntervalMs = 60000; /* 60 seconds default */

/* Area-change auto-save (independent of the interval timer). Tracks
 * the last-observed (area, room) and saves to the ring whenever it
 * changes. Helps with the crash-on-load-then-lose-an-hour case Jester
 * flagged. */
static int sAutoOnAreaChange = 1;
static u8 sLastSeenArea = 0xFF;
static u8 sLastSeenRoom = 0xFF;

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

static const void* FindRegionInSnapshot(const Slot* s, const void* target_ptr) {
    if (!s || !s->snapshot)
        return NULL;
    size_t offset = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        if (sRegions[i].ptr == target_ptr) {
            return s->snapshot + offset;
        }
        offset += sRegions[i].size;
    }
    return NULL;
}

static void CaptureAuxState(Slot* s) {
    if (!s->aux) {
        s->aux = (SlotAuxState*)malloc(sizeof(SlotAuxState));
    }
    if (!s->aux)
        return;
    memcpy(s->aux->bgPltt, gBgPltt, sizeof(s->aux->bgPltt));
    memcpy(s->aux->objPltt, gObjPltt, sizeof(s->aux->objPltt));
    memcpy(s->aux->oamMem, gOamMem, sizeof(s->aux->oamMem));
    memcpy(s->aux->paletteBuffer, gPaletteBuffer, sizeof(s->aux->paletteBuffer));
    s->aux->usedPalettes = gUsedPalettes;
    s->aux->screen = gScreen;
    s->aux->fadeControl = gFadeControl;
    memcpy(s->aux->unk_020354C0, gUnk_020354C0, sizeof(s->aux->unk_020354C0));
    s->aux->oamControls = gOAMControls;
    s->aux->vblankDma = gVBlankDMA;
    s->aux->updateVisibleTiles = gUpdateVisibleTiles;
    s->aux->ui = gUI;
    s->aux->hud = gHUD;
    s->aux->gfxSlots = gGFXSlots;
    s->aux->area = gArea;
    s->aux->roomVars = gRoomVars;
    memcpy(s->aux->entityLists, gEntityLists, sizeof(s->aux->entityLists));
    s->aux->entCount = gEntCount;
    s->aux->managerCount = gManagerCount;
    s->aux->mapBottom = gMapBottom;
    s->aux->mapTop = gMapTop;
    memcpy(s->aux->roomMemory, gRoomMemory, sizeof(s->aux->roomMemory));
}

static void RestoreAuxState(const Slot* s) {
    if (!s || !s->aux)
        return;
    memcpy(gBgPltt, s->aux->bgPltt, sizeof(s->aux->bgPltt));
    memcpy(gObjPltt, s->aux->objPltt, sizeof(s->aux->objPltt));
    memcpy(gOamMem, s->aux->oamMem, sizeof(s->aux->oamMem));
    memcpy(gPaletteBuffer, s->aux->paletteBuffer, sizeof(s->aux->paletteBuffer));
    gUsedPalettes = s->aux->usedPalettes;
    gScreen = s->aux->screen;
    gFadeControl = s->aux->fadeControl;
    memcpy(gUnk_020354C0, s->aux->unk_020354C0, sizeof(s->aux->unk_020354C0));
    gOAMControls = s->aux->oamControls;
    gVBlankDMA = s->aux->vblankDma;
    gUpdateVisibleTiles = s->aux->updateVisibleTiles;
    gUI = s->aux->ui;
    gHUD = s->aux->hud;
    gGFXSlots = s->aux->gfxSlots;
    gArea = s->aux->area;
    gRoomVars = s->aux->roomVars;
    memcpy(gEntityLists, s->aux->entityLists, sizeof(s->aux->entityLists));
    gEntCount = s->aux->entCount;
    gManagerCount = s->aux->managerCount;
    gMapBottom = s->aux->mapBottom;
    gMapTop = s->aux->mapTop;
    memcpy(gRoomMemory, s->aux->roomMemory, sizeof(s->aux->roomMemory));
}

static int Snapshot_Capture(Slot* s) {
    const size_t total = TotalRegionBytes();
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (s->snapshot == NULL) {
            s->bytes = 0;
            s->valid = 0;
            fprintf(stderr, "[quicksave] alloc failed (%zu bytes)\n", total);
            return 0;
        }
        s->bytes = total;
    }
    u8* dst = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(dst, sRegions[i].ptr, sRegions[i].size);
        dst += sRegions[i].size;
    }
    CaptureAuxState(s);
    s->valid = 1;
    s->saved_at_unix = (u64)time(NULL);
    s->saved_entities_base = (u64)(uintptr_t)gEntities;
    return 1;
}

static int Snapshot_MatchesCurrent(const Slot* s, const char** region, size_t* offset, u8* expected, u8* actual) {
    const u8* src;
    size_t i;

    if (!s->valid || s->snapshot == NULL || s->bytes != TotalRegionBytes()) {
        *region = "snapshot";
        *offset = 0;
        *expected = 0;
        *actual = 0;
        return 0;
    }

    src = s->snapshot;
    for (i = 0; i < NUM_REGIONS; i++) {
        const u8* current = (const u8*)sRegions[i].ptr;
        size_t j;

        if (memcmp(src, current, sRegions[i].size) == 0) {
            src += sRegions[i].size;
            continue;
        }
        for (j = 0; j < sRegions[i].size && src[j] == current[j]; j++) {}
        *region = sRegions[i].name;
        *offset = j;
        *expected = src[j];
        *actual = current[j];
        return 0;
    }
    return 1;
}

static int Snapshot_Restore(const Slot* s) {
    if (!s->valid || s->snapshot == NULL || s->bytes != TotalRegionBytes()) {
        return 0;
    }

    const u8 priorTask = gMain.task;
    const bool crossProcess = (s->saved_entities_base != 0 && (uintptr_t)s->saved_entities_base != (uintptr_t)gEntities);
    const bool needsCleanRoomEntry = crossProcess || (priorTask != TASK_GAME) || (s->aux == NULL);

    if (needsCleanRoomEntry) {
        const SaveFile* savedSave = (const SaveFile*)FindRegionInSnapshot(s, &gSave);
        const PlayerEntity* savedPlayer = (const PlayerEntity*)FindRegionInSnapshot(s, &gPlayerEntity);
        const PlayerState* savedPlayerState = (const PlayerState*)FindRegionInSnapshot(s, &gPlayerState);
        const Main* savedMain = (const Main*)FindRegionInSnapshot(s, &gMain);
        const RoomControls* savedRoomControls = (const RoomControls*)FindRegionInSnapshot(s, &gRoomControls);
        const RoomTransition* savedRoomTransition = (const RoomTransition*)FindRegionInSnapshot(s, &gRoomTransition);
        const u32* savedRand = (const u32*)FindRegionInSnapshot(s, &gRand);
        const u64* savedPracticeFrame = (const u64*)FindRegionInSnapshot(s, &gPracticeFrame);

        if (!savedSave || !savedPlayer || !savedPlayerState || !savedMain || !savedRoomControls ||
            !savedRoomTransition) {
            return 0;
        }
        if (savedMain->task != TASK_GAME) {
            fprintf(stderr, "[quicksave] refusing to restore non-gameplay snapshot (saved task=%u)\n",
                    (unsigned)savedMain->task);
            return 0;
        }

        memcpy(&gSave, savedSave, sizeof(gSave));
        if (savedRand)
            gRand = *savedRand;
        if (savedPracticeFrame)
            gPracticeFrame = *savedPracticeFrame;

        /* Populate gSave.saved_status from the snapshot's live room/player state
         * so GameTask_Transition -> GameTask_Init -> GameMain_InitRoom spawns
         * Link in the exact room, coordinates, layer, direction, and Minish form
         * with freshly initialized pointers, palettes, and display controls. */
        if (savedRoomTransition->player_status.area_next != 0 || savedRoomTransition->player_status.room_next != 0 ||
            savedRoomControls->area == 0) {
            gSave.saved_status = savedRoomTransition->player_status;
        }
        gSave.saved_status.area_next = savedRoomControls->area;
        gSave.saved_status.room_next = savedRoomControls->room;
        if (savedMain->state == GAMETASK_MAIN && savedPlayer->base.x.HALF.HI >= savedRoomControls->origin_x &&
            savedPlayer->base.y.HALF.HI >= savedRoomControls->origin_y &&
            (savedPlayer->base.x.HALF.HI != 0 || savedPlayer->base.y.HALF.HI != 0)) {
            gSave.saved_status.start_pos_x = (s16)(savedPlayer->base.x.HALF.HI - savedRoomControls->origin_x);
            gSave.saved_status.start_pos_y = (s16)(savedPlayer->base.y.HALF.HI - savedRoomControls->origin_y);
        }
        if (savedPlayer->base.collisionLayer != 0) {
            gSave.saved_status.layer = savedPlayer->base.collisionLayer;
        } else if (gSave.saved_status.layer == 0) {
            gSave.saved_status.layer = 1;
        }
        gSave.saved_status.start_anim = savedPlayer->base.animationState;
        gSave.saved_status.spawn_type = (savedPlayerState->flags & PL_MINISH) ? PL_SPAWN_MINISH : PL_SPAWN_DEFAULT;
        gSave.initialized = 1;
        gSave.invalid = 0;
        if (gSave.stats.health == 0) {
            gSave.stats.health = gSave.stats.maxHealth ? gSave.stats.maxHealth : 24;
        }

        InitFade();
        SetTask(TASK_GAME);
        Port_DebugAction_ArmWarpNudge();
        fprintf(stderr, "[quicksave] clean room-entry restore -> area=0x%02x room=0x%02x pos=(%d,%d) layer=%d\n",
                (unsigned)gSave.saved_status.area_next, (unsigned)gSave.saved_status.room_next,
                (int)gSave.saved_status.start_pos_x, (int)gSave.saved_status.start_pos_y,
                (int)gSave.saved_status.layer);
        return 1;
    }

    const u8* src = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(sRegions[i].ptr, src, sRegions[i].size);
        src += sRegions[i].size;
    }
    RestoreAuxState(s);
    Port_RestorePlayerHitbox();
    return 1;
}

/* Headless deterministic restore/replay gate. With no external input, advance
 * the same 120 engine frames twice from one snapshot and require every saved
 * byte to agree. Runs here so capture/restore stays on a frame boundary. */
static int QuickSaveReplayTestTick(void) {
    enum {
        SETTLE_FRAMES = 30,
        DEFAULT_REPLAY_FRAMES = 120,
        MAX_REPLAY_FRAMES = 3600,
    };
    static int enabled = -1;
    static int phase = 0;
    static unsigned int phaseFrame = 0;
    static unsigned int replayFrames = DEFAULT_REPLAY_FRAMES;
    static Slot start;
    static Slot expectedEnd;
    const char* region;
    size_t offset;
    u8 expected;
    u8 actual;

    if (enabled < 0) {
        const char* env = getenv("TMC_REPRO_QUICKSAVE_ROUNDTRIP");
        enabled = env != NULL && env[0] != '\0' && strcmp(env, "0") != 0;
        if (enabled) {
            const char* frames = getenv("TMC_REPRO_QUICKSAVE_FRAMES");
            char* end;
            unsigned long requested = frames != NULL ? strtoul(frames, &end, 10) : 0;
            if (frames != NULL && requested > 0 && requested <= MAX_REPLAY_FRAMES && end != frames && *end == '\0') {
                replayFrames = (unsigned int)requested;
            }
            fprintf(stderr, "[quicksave-test] enabled: settle=%d replay=%u frames\n", SETTLE_FRAMES, replayFrames);
        }
    }
    if (!enabled) {
        return 0;
    }

    if (phase == 0 && gMain.task != TASK_GAME) {
        phaseFrame = 0;
        return 1;
    }

    phaseFrame++;
    if (phase == 0 && phaseFrame >= SETTLE_FRAMES) {
        if (!Snapshot_Capture(&start)) {
            fprintf(stderr, "[quicksave-test] FAIL: initial capture failed\n");
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] captured start (%zu bytes)\n", start.bytes);
        phase = 1;
        phaseFrame = 0;
    } else if (phase == 1 && phaseFrame >= replayFrames) {
        if (!Snapshot_Capture(&expectedEnd) || !Snapshot_Restore(&start)) {
            fprintf(stderr, "[quicksave-test] FAIL: endpoint capture or restore failed\n");
            fflush(stderr);
            _Exit(1);
        }
        if (!Snapshot_MatchesCurrent(&start, &region, &offset, &expected, &actual)) {
            fprintf(stderr, "[quicksave-test] FAIL: restore mismatch %s+0x%zx expected=%02x actual=%02x\n", region,
                    offset, (unsigned)expected, (unsigned)actual);
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] restored start; replaying\n");
        phase = 2;
        phaseFrame = 0;
    } else if (phase == 2 && phaseFrame >= replayFrames) {
        if (!Snapshot_MatchesCurrent(&expectedEnd, &region, &offset, &expected, &actual)) {
            fprintf(stderr, "[quicksave-test] FAIL: replay mismatch %s+0x%zx expected=%02x actual=%02x\n", region,
                    offset, (unsigned)expected, (unsigned)actual);
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] PASS: %u-frame replay byte-identical (%zu bytes)\n", replayFrames,
                expectedEnd.bytes);
        fflush(stderr);
        _Exit(0);
    }

    return 1;
}

/* Region tag for the state header + per-region state filenames. EU/JP get
 * their own state files (like tmc_eu.sav / tmc_jp.sav) so sessions never
 * even see another region's states; USA keeps the legacy names. */
static u32 ActiveRegionTag(void) {
    if (REGION_IS_EU)
        return 2;
    if (REGION_IS_JP)
        return 3;
    return 1; /* USA */
}

static void SlotFilename(int slot, char* out, size_t cap) {
    const char* prefix = REGION_IS_EU ? "state_eu" : REGION_IS_JP ? "state_jp" : "state";
    if (slot == AUTO_RESUME_SLOT) {
        snprintf(out, cap, "%s_resume.bin", prefix);
    } else if (slot >= AUTO_SLOT_BASE && slot < AUTO_SLOT_BASE + NUM_AUTO_SLOTS) {
        snprintf(out, cap, "%s_auto_%d.bin", prefix, slot - AUTO_SLOT_BASE);
    } else if (slot == 0) {
        snprintf(out, cap, "%s_quick.bin", prefix);
    } else {
        snprintf(out, cap, "%s_%d.bin", prefix, slot);
    }
}

static int WriteSlotToDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    Slot* s = &sSlots[slot];
    if (!s->valid || s->snapshot == NULL)
        return 0;

    char path[128];
    SlotFilename(slot, path, sizeof(path));
    char tmpPath[136];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    FILE* f = fopen(tmpPath, "wb");
    if (!f) {
        fprintf(stderr, "[quicksave] open %s for write failed\n", tmpPath);
        return 0;
    }
    const u32 magic = MAGIC;
    const u32 version = VERSION;
    const u32 total = (u32)s->bytes;
    const u64 saved_at = s->saved_at_unix;
    /* gEntities base — needed to fix up internal pointers when the
     * file is loaded by a later process (different ASLR base). The
     * captured bytes still contain prev/next/child/parent pointers
     * into the old process's gEntities[]; without fixup, restoring
     * them and running the entity-update loop dereferences unmapped
     * memory. */
    const u64 entities_base = (u64)(uintptr_t)gEntities;
    const u32 region_tag = ActiveRegionTag();
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 || fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&total, sizeof(total), 1, f) != 1 || fwrite(&saved_at, sizeof(saved_at), 1, f) != 1 ||
        fwrite(&entities_base, sizeof(entities_base), 1, f) != 1 ||
        fwrite(&region_tag, sizeof(region_tag), 1, f) != 1) {
        fprintf(stderr, "[quicksave] header write failed for %s\n", tmpPath);
        fclose(f);
        remove(tmpPath);
        return 0;
    }
    const size_t written = fwrite(s->snapshot, 1, s->bytes, f);
    fflush(f);
#if !defined(_WIN32)
    fsync(fileno(f));
#endif
    fclose(f);
    if (written != s->bytes) {
        fprintf(stderr, "[quicksave] short write %s (%zu/%zu)\n", tmpPath, written, s->bytes);
        remove(tmpPath);
        return 0;
    }
    if (rename(tmpPath, path) != 0) {
        fprintf(stderr, "[quicksave] rename %s -> %s failed\n", tmpPath, path);
        remove(tmpPath);
        return 0;
    }
    return 1;
}

/* Reads the fixed 4-field slot header (magic, version, total, saved_at).
 * Returns 1 if all four fields read, 0 on short read. Field values are
 * not validated and no diagnostics are emitted — callers decide. */
static int ReadSlotHeader(FILE* f, u32* magic, u32* version, u32* total, u64* saved_at) {
    return fread(magic, sizeof(*magic), 1, f) == 1 && fread(version, sizeof(*version), 1, f) == 1 &&
           fread(total, sizeof(*total), 1, f) == 1 && fread(saved_at, sizeof(*saved_at), 1, f) == 1;
}

static int ReadSlotFromDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    u64 saved_entities_base = 0;
    if (!ReadSlotHeader(f, &magic, &version, &total, &saved_at)) {
        fprintf(stderr, "[quicksave] short read on %s header, ignoring slot file\n", path);
        fclose(f);
        return 0;
    }
    if (magic != MAGIC || version != VERSION || total != (u32)TotalRegionBytes()) {
        fclose(f);
        return 0;
    }
    if (fread(&saved_entities_base, sizeof(saved_entities_base), 1, f) != 1) {
        fprintf(stderr, "[quicksave] short read on %s entity-base header, ignoring slot file\n", path);
        fclose(f);
        return 0;
    }
    {
        u32 region_tag = 0;
        if (fread(&region_tag, sizeof(region_tag), 1, f) != 1) {
            fprintf(stderr, "[quicksave] short read on %s region header, ignoring slot file\n", path);
            fclose(f);
            return 0;
        }
        if (region_tag != ActiveRegionTag()) {
            fprintf(stderr, "[quicksave] %s was saved in a different ROM region (%u != %u) — refusing load\n", path,
                    region_tag, ActiveRegionTag());
            fclose(f);
            return 0;
        }
    }
    Slot* s = &sSlots[slot];
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (!s->snapshot) {
            s->bytes = 0;
            s->valid = 0;
            fclose(f);
            return 0;
        }
        s->bytes = total;
    }
    const size_t got = fread(s->snapshot, 1, total, f);
    fclose(f);
    if (got != total) {
        fprintf(stderr, "[quicksave] short read on %s (%zu/%u bytes), ignoring slot file\n", path, got, total);
        s->valid = 0;
        return 0;
    }
    free(s->aux);
    s->aux = NULL;
    s->valid = 1;
    s->saved_at_unix = saved_at;
    s->saved_entities_base = saved_entities_base;
    return 1;
}

/* ============================================================
 *   Public API
 * ============================================================ */

static SDL_Mutex* sSaveMutex = NULL;
static SDL_Semaphore* sLifecycleDoneSem = NULL;
static bool sLifecycleRequestPending = false;

static void EnsureSyncObjects(void) {
    if (!sSaveMutex)
        sSaveMutex = SDL_CreateMutex();
    if (!sLifecycleDoneSem)
        sLifecycleDoneSem = SDL_CreateSemaphore(0);
}

int Port_QuickSave_SaveSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    EnsureSyncObjects();
    SDL_LockMutex(sSaveMutex);
    if (!Snapshot_Capture(&sSlots[slot])) {
        SDL_UnlockMutex(sSaveMutex);
        return 0;
    }
    /* Best-effort disk persistence — failure is non-fatal, the in-memory
     * snapshot still works for the session. */
    WriteSlotToDisk(slot);
    fprintf(stderr, "[quicksave] slot %d saved (%zu bytes)\n", slot, sSlots[slot].bytes);
    SDL_UnlockMutex(sSaveMutex);
    return 1;
}

int Port_QuickSave_LoadSlot(int slot) {
    /* Refuse any state restore under Console-Parity — covers the menu/imgui
     * load buttons too, not just the F-key hotkeys. */
    if (Port_Config_GetConsoleParity())
        return 0;
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    EnsureSyncObjects();
    SDL_LockMutex(sSaveMutex);
    if (!sSlots[slot].valid) {
        /* Try loading from disk first — handles "fresh launch, never
         * saved this session but slot exists on disk from last run". */
        if (!ReadSlotFromDisk(slot)) {
            fprintf(stderr, "[quicksave] slot %d empty\n", slot);
            SDL_UnlockMutex(sSaveMutex);
            return 0;
        }
    }
    if (!Snapshot_Restore(&sSlots[slot])) {
        fprintf(stderr, "[quicksave] slot %d restore failed\n", slot);
        SDL_UnlockMutex(sSaveMutex);
        return 0;
    }
    fprintf(stderr, "[quicksave] slot %d restored\n", slot);
    SDL_UnlockMutex(sSaveMutex);
    {
        /* Tell the Reborn-parity layer a resume just happened so it
         * can swallow the next queued Ezlo hint (if that toggle is on). */
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
    }
    return 1;
}

int Port_QuickSave_HasSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (sSlots[slot].valid)
        return 1;
    /* Probe disk so the menu can label populated-on-disk slots correctly
     * before the user touches them. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* ---- Practice point -------------------------------------------------- *
 * A dedicated in-memory snapshot for the speedrun practice mode, kept
 * separate from the F1..F5 user slots so practising a segment never clobbers
 * a manual save. In-process only (no disk, no pointer fixup needed — the
 * entities base is unchanged within a run), so set/reload is a sub-ms memcpy.
 * Driven from port_practice.c via the Port_Practice_SetPoint/LoadPoint API. */
static Slot sPracticeSlot;

int Port_QuickSave_SavePractice(void) {
    if (!Snapshot_Capture(&sPracticeSlot))
        return 0;
    fprintf(stderr, "[quicksave] practice point set (%zu bytes)\n", sPracticeSlot.bytes);
    return 1;
}

int Port_QuickSave_LoadPractice(void) {
    if (Port_Config_GetConsoleParity())
        return 0;
    if (!sPracticeSlot.valid) {
        fprintf(stderr, "[quicksave] practice point empty\n");
        return 0;
    }
    if (!Snapshot_Restore(&sPracticeSlot)) {
        fprintf(stderr, "[quicksave] practice point restore failed\n");
        return 0;
    }
    {
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
    }
    return 1;
}

int Port_QuickSave_HasPractice(void) {
    return sPracticeSlot.valid ? 1 : 0;
}

u64 Port_QuickSave_SlotTimestamp(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (sSlots[slot].valid)
        return sSlots[slot].saved_at_unix;
    /* Probe the disk file's timestamp header so the menu can show
     * "last saved" even for slots that haven't been loaded into memory. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    if (ReadSlotHeader(f, &magic, &version, &total, &saved_at)) {
        fclose(f);
        return saved_at;
    }
    fclose(f);
    return 0;
}

/* Legacy single-slot API — slot 0 is the F5/F6 quicksave. */
int Port_QuickSave(void) {
    return Port_QuickSave_SaveSlot(0);
}
int Port_QuickLoad(void) {
    return Port_QuickSave_LoadSlot(0);
}
int Port_QuickSave_HasSnapshot(void) {
    return Port_QuickSave_HasSlot(0);
}

int Port_QuickSave_DeleteSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    EnsureSyncObjects();
    SDL_LockMutex(sSaveMutex);
    sSlots[slot].valid = 0;
    char path[128];
    SlotFilename(slot, path, sizeof(path));
    remove(path);
    SDL_UnlockMutex(sSaveMutex);
    return 1;
}

int Port_QuickSave_SaveAutoResume(void) {
    if (gMain.task != TASK_GAME || (gMain.state != GAMETASK_MAIN && gMain.state != GAMETASK_EXIT)) {
        /* Outside active gameplay (Title / File Select / uninitialized transition):
         * do not overwrite or delete an existing valid resume save. */
        return 0;
    }
    static u32 sLastResumeFrame = 0xFFFFFFFF;
    static u64 sLastResumeTime = 0;
    const u64 now = SDL_GetTicks();
    if (sLastResumeFrame == gMain.ticks && (now - sLastResumeTime) < 1000) {
        return 1; /* Already saved this frame within 1 sec (debounce back-to-back lifecycle events) */
    }
    sLastResumeFrame = gMain.ticks;
    sLastResumeTime = now;
    return Port_QuickSave_SaveSlot(AUTO_RESUME_SLOT);
}

void Port_QuickSave_RequestLifecycleAutoResume(void) {
    if (!Port_Config_GetAutoSaveOnExit())
        return;
    EnsureSyncObjects();
    sLifecycleRequestPending = true;
    /* Wait up to 150ms for the game thread to reach VBlank and capture frame-boundary state */
    if (SDL_WaitSemaphoreTimeout(sLifecycleDoneSem, 150)) {
        sLifecycleRequestPending = false;
        return;
    }
    /* Fallback: if game thread was already paused or suspended by SDL, save directly under mutex */
    sLifecycleRequestPending = false;
    Port_QuickSave_SaveAutoResume();
}

void Port_QuickSave_CheckLifecycleRequest(void) {
    if (sLifecycleRequestPending) {
        sLifecycleRequestPending = false;
        Port_QuickSave_SaveAutoResume();
        if (sLifecycleDoneSem) {
            SDL_SignalSemaphore(sLifecycleDoneSem);
        }
    }
}

int Port_QuickSave_LoadAutoResume(void) {
    if (Port_QuickSave_HasSlot(AUTO_RESUME_SLOT)) {
        if (Port_QuickSave_LoadSlot(AUTO_RESUME_SLOT))
            return 1;
    }
    /* Fallback to newest auto-save ring slot if resume save missing (e.g. killed abruptly) */
    u64 best_time = 0;
    int best_slot = -1;
    for (int s = AUTO_SLOT_BASE; s < AUTO_SLOT_BASE + NUM_AUTO_SLOTS; s++) {
        u64 t = Port_QuickSave_SlotTimestamp(s);
        if (t > best_time) {
            best_time = t;
            best_slot = s;
        }
    }
    if (best_slot >= 0) {
        fprintf(stderr, "[autosave] falling back to ring slot %d (timestamp %llu)\n", best_slot,
                (unsigned long long)best_time);
        return Port_QuickSave_LoadSlot(best_slot);
    }
    return 0;
}

int Port_QuickSave_HasAutoResume(void) {
    if (Port_QuickSave_HasSlot(AUTO_RESUME_SLOT))
        return 1;
    for (int s = AUTO_SLOT_BASE; s < AUTO_SLOT_BASE + NUM_AUTO_SLOTS; s++) {
        if (Port_QuickSave_HasSlot(s))
            return 1;
    }
    return 0;
}

/* Auto-save — call once per frame from VBlankIntrWait. Saves to the
 * next slot in the auto-save ring if enabled and the configured
 * interval has elapsed since the last auto-save. */
static void TakeAutoSnapshot(const char* reason) {
    const int slot = sAutoNextSlot;
    sAutoNextSlot++;
    if (sAutoNextSlot >= AUTO_SLOT_BASE + NUM_AUTO_SLOTS) {
        sAutoNextSlot = AUTO_SLOT_BASE;
    }
    if (Port_QuickSave_SaveSlot(slot)) {
        fprintf(stderr, "[autosave] saved to ring slot %d (%s)\n", slot, reason);
    }
    /* Toast notification for periodic timed auto-save */
    if (strcmp(reason, "interval") == 0) {
        extern void Port_DebugMenu_ToastFromExternal(const char* msg);
        extern bool Port_IsItalian(void);
        const char* msg = Port_IsItalian() ? "Salvataggio periodico completato" : "Periodic auto-save completed";
        Port_DebugMenu_ToastFromExternal(msg);
    }
}

void Port_QuickSave_AutoTick(void) {
    if (QuickSaveReplayTestTick() || !sAutoEnabled)
        return;
    if (gMain.task != TASK_GAME || gMain.state != GAMETASK_MAIN)
        return;
    const u64 now = SDL_GetTicks();

    /* Area-change trigger. gRoomControls is the engine's source-of-
     * truth for the current area/room; we just compare against the
     * last value we observed and fire a snapshot on transition. The
     * first poll seeds the cache without saving (sLastSeen* both
     * 0xFF) so we don't double-save on boot. */
    if (sAutoOnAreaChange) {
        /* gRoomControls is declared in include/room.h, already in scope
         * via include/save.h above. */
        const u8 area = gRoomControls.area;
        const u8 room = gRoomControls.room;
        if (sLastSeenArea == 0xFF && sLastSeenRoom == 0xFF) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
        } else if (area != sLastSeenArea || room != sLastSeenRoom) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
            /* Cooldown: avoid thrashing disk writes on rapid door transitions */
            if (sAutoLastAreaChangeTicksMs == 0 || now - sAutoLastAreaChangeTicksMs >= 10000ULL) {
                sAutoLastAreaChangeTicksMs = now;
                TakeAutoSnapshot("area-change");
            }
        }
    }

    /* Interval trigger (runs independently of area changes). */
    if (sAutoLastIntervalTicksMs == 0) {
        sAutoLastIntervalTicksMs = now;
        return;
    }
    if (now - sAutoLastIntervalTicksMs < sAutoIntervalMs)
        return;
    sAutoLastIntervalTicksMs = now;
    TakeAutoSnapshot("interval");
}

int Port_QuickSave_AutoOnAreaChangeEnabled(void) {
    return sAutoOnAreaChange;
}
void Port_QuickSave_SetAutoOnAreaChange(int on) {
    sAutoOnAreaChange = on ? 1 : 0;
    /* Reset the seen-area cache when toggled on so the next change
     * doesn't fire spuriously against pre-toggle history. */
    if (on) {
        sLastSeenArea = 0xFF;
        sLastSeenRoom = 0xFF;
    }
}

void Port_QuickSave_SetAutoEnabled(int enabled) {
    sAutoEnabled = enabled ? 1 : 0;
    if (enabled) {
        sAutoLastIntervalTicksMs = SDL_GetTicks();
        sAutoLastAreaChangeTicksMs = SDL_GetTicks();
    }
}

int Port_QuickSave_AutoEnabled(void) {
    return sAutoEnabled;
}

void Port_QuickSave_SetAutoIntervalMs(u32 ms) {
    if (ms < 5000)
        ms = 5000; /* clamp to 5s minimum — anything
                      faster would thrash on busy
                      frames and risk visible hitches. */
    if (ms > 600000)
        ms = 600000; /* 10 minute cap */
    sAutoIntervalMs = ms;
    sAutoLastIntervalTicksMs = SDL_GetTicks();
}

u32 Port_QuickSave_AutoIntervalMs(void) {
    return sAutoIntervalMs;
}

int Port_QuickSave_SlotCount(void) {
    return NUM_SLOTS;
}
int Port_QuickSave_AutoSlotBase(void) {
    return AUTO_SLOT_BASE;
}
int Port_QuickSave_AutoSlotCount(void) {
    return NUM_AUTO_SLOTS;
}
