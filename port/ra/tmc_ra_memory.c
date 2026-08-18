#include "tmc_ra_memory.h"
#include "tmc_ra_memory_manifest.generated.h"

#include "area.h"
#include "backgroundAnimations.h"
#include "common.h"
#include "entity.h"
#include "fade.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "player.h"
#include "port_gba_mem.h"
#include "port_save.h"
#include "room.h"
#include "screen.h"
#include "save.h"
#include "script.h"
#include "sound.h"
#include "structures.h"
#include "tileMap.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

extern u32 gRand;
extern ScriptExecutionContext gScriptExecutionContextArray[0x20];

_Static_assert(sizeof(Main) == 0x0e, "gMain GBA layout drift");
_Static_assert(offsetof(Main, ticks) == 0x0c, "gMain.ticks GBA offset drift");
_Static_assert(sizeof(Input) == 0x08, "gInput GBA layout drift");
_Static_assert(offsetof(Input, menuScrollTimer) == 0x07, "gInput timer GBA offset drift");
_Static_assert(offsetof(FadeControl, mask) == 0x04, "gFadeControl.mask GBA offset drift");
_Static_assert(offsetof(FadeControl, win_outside_cnt) == 0x18, "gFadeControl tail GBA offset drift");
_Static_assert(offsetof(RoomControls, camera_target) == 0x30, "gRoomControls camera GBA offset drift");
_Static_assert(sizeof(RoomTransition) == 0xb0, "gRoomTransition GBA layout drift");
_Static_assert(sizeof(RoomMemory) == 0x08, "gRoomMemory GBA layout drift");
_Static_assert(sizeof(RoomMemory) * 8 == 0x40, "gRoomMemory array GBA layout drift");
_Static_assert(sizeof(SaveFile) == 0x500, "gSave host layout drift");
_Static_assert(offsetof(SaveFile, saved_status) == 0x088, "gSave.saved_status GBA offset drift");
_Static_assert(offsetof(SaveFile, stats) == 0x0a8, "gSave.stats GBA offset drift");
_Static_assert(offsetof(SaveFile, fillerCC) == 0x0cc, "gSave.fillerCC GBA offset drift");
_Static_assert(offsetof(SaveFile, figurines) == 0x0ce, "gSave.figurines GBA offset drift");
_Static_assert(offsetof(SaveFile, kinstones) == 0x114, "gSave.kinstones GBA offset drift");
_Static_assert(offsetof(SaveFile, flags) == 0x25b, "gSave.flags host offset drift");
_Static_assert(offsetof(SaveFile, dungeonKeys) == 0x45b, "gSave.dungeonKeys host offset drift");
_Static_assert(offsetof(SaveFile, filler4ac) == 0x4ac, "gSave reviewed boundary drift");
_Static_assert(offsetof(Entity, kind) == 0x10, "Entity scalar prefix host offset drift");
_Static_assert(offsetof(Entity, hitbox) == 0x50, "Entity hitbox host offset drift");
_Static_assert(offsetof(Entity, animIndex) == 0x70, "Entity animation host offset drift");
_Static_assert(offsetof(Entity, animPtr) == 0x78, "Entity animation pointer host offset drift");
_Static_assert(offsetof(Entity, spriteVramOffset) == 0x80, "Entity scalar suffix host offset drift");
_Static_assert(offsetof(Entity, myHeap) == 0x88, "Entity heap pointer host offset drift");
_Static_assert(offsetof(GenericEntity, field_0x68) == 0x90, "GenericEntity scalar tail host offset drift");
_Static_assert(offsetof(GenericEntity, cutsceneBeh) == 0xb0, "GenericEntity script overlay host offset drift");
_Static_assert(offsetof(PlayerEntity, pulledJarEntity) == 0x98, "PlayerEntity pointer host offset drift");
_Static_assert(offsetof(PlayerEntity, carriedEntity) == 0xa0, "PlayerEntity pointer host offset drift");
_Static_assert(offsetof(PlayerState, item) == 0x30, "PlayerState item host offset drift");
_Static_assert(offsetof(PlayerState, flags) == 0x38, "PlayerState scalar middle host offset drift");
_Static_assert(offsetof(PlayerState, lilypad) == 0x90, "PlayerState lilypad host offset drift");
_Static_assert(offsetof(PlayerState, field_0x88) == 0x98, "PlayerState scalar suffix host offset drift");
_Static_assert(offsetof(PlayerState, playerInput) == 0xa0, "PlayerState input host offset drift");
_Static_assert(offsetof(PlayerInput, playerMacro) == 0x10, "PlayerInput pointer host offset drift");
_Static_assert(offsetof(PlayerState, chargeState) == 0xb8, "PlayerState charge host offset drift");
_Static_assert(offsetof(PlayerState, framestate) == 0xc0, "PlayerState final scalar host offset drift");
_Static_assert(offsetof(ItemBehavior, field_0x18) == 0x18, "ItemBehavior pointer host offset drift");
_Static_assert(offsetof(Message, textIndex) == 0x08, "gMessage text offset drift");
_Static_assert(offsetof(Message, field_0x1c) == 0x1c, "gMessage tail offset drift");
_Static_assert(offsetof(Menu, field_0xc) == 0x10, "gMenu pointer host offset drift");
_Static_assert(offsetof(HUD, elements) == 0x38, "gHUD elements host offset drift");
_Static_assert(offsetof(UIElement, framePtr) == 0x18, "UIElement frame pointer host offset drift");
_Static_assert(offsetof(UIElement, firstTile) == 0x28, "UIElement tile pointer host offset drift");
_Static_assert(offsetof(UI, currentRoomProperties) == 0x10, "gUI room properties host offset drift");
_Static_assert(offsetof(UI, roomControls) == 0x28, "gUI room controls host offset drift");
_Static_assert(offsetof(UI, gfxSlotList) == 0x68, "gUI gfx slots host offset drift");
_Static_assert(offsetof(UI, palettes) == 0x330, "gUI palettes host offset drift");
_Static_assert(offsetof(UI, unk_2a8) == 0x370, "gUI scalar tail host offset drift");
_Static_assert(offsetof(MapLayer, mapData) == 0x08, "MapLayer map host offset drift");
_Static_assert(offsetof(MapLayer, collisionData) == 0x2008, "MapLayer collision host offset drift");
_Static_assert(offsetof(MapLayer, mapDataOriginal) == 0x3008, "MapLayer original host offset drift");
_Static_assert(offsetof(MapLayer, tileTypes) == 0x5008, "MapLayer types host offset drift");
_Static_assert(offsetof(MapLayer, tileIndices) == 0x6008, "MapLayer indices host offset drift");
_Static_assert(offsetof(MapLayer, subTiles) == 0x7008, "MapLayer subtiles host offset drift");
_Static_assert(offsetof(MapLayer, actTiles) == 0xb008, "MapLayer act host offset drift");
_Static_assert(offsetof(Area, lightType) == 0x0c, "Area light host offset drift");
_Static_assert(offsetof(Area, filler3) == 0x0e, "Area portal filler host offset drift");
_Static_assert(offsetof(Area, unk28) == 0x28, "Area hint host offset drift");
_Static_assert(offsetof(Area, roomResInfos) == 0x40, "Area rooms host offset drift");
_Static_assert(offsetof(Area, currentRoomInfo) == 0xe40, "Area current room host offset drift");
_Static_assert(offsetof(Area, pCurrentRoomInfo) == 0xe78, "Area current room pointer host offset drift");
_Static_assert(offsetof(Area, bgm) == 0xe80, "Area BGM host offset drift");
_Static_assert(offsetof(RoomResInfo, tileSet) == 0x08, "RoomResInfo pointer host offset drift");
_Static_assert(offsetof(RoomVars, currentAreaDroptable) == 0x48, "RoomVars drop table host offset drift");
_Static_assert(offsetof(RoomVars, animFlags) == 0x68, "RoomVars flags host offset drift");
_Static_assert(offsetof(RoomVars, properties) == 0x70, "RoomVars properties host offset drift");
_Static_assert(offsetof(RoomVars, entityRails) == 0xb0, "RoomVars rails host offset drift");
_Static_assert(offsetof(RoomVars, puzzleEntities) == 0xf0, "RoomVars puzzle host offset drift");
_Static_assert(offsetof(OAMControls, oam) == 0x20, "gOAMControls OAM host offset drift");
_Static_assert(offsetof(Screen, bg0) == 0x08, "gScreen BG0 host offset drift");
_Static_assert(offsetof(Screen, bg1) == 0x18, "gScreen BG1 host offset drift");
_Static_assert(offsetof(Screen, bg2) == 0x28, "gScreen BG2 host offset drift");
_Static_assert(offsetof(Screen, bg3) == 0x38, "gScreen BG3 host offset drift");
_Static_assert(offsetof(Screen, controls) == 0x48, "gScreen controls host offset drift");
_Static_assert(offsetof(Screen, vBlankDMA) == 0x80, "gScreen VBlank DMA host offset drift");
_Static_assert(offsetof(BgAnimation, unk_4) == 0x08, "gBgAnimations scalar host offset drift");
_Static_assert(offsetof(BgAnimation, timer) == 0x0a, "gBgAnimations timer host offset drift");
_Static_assert(sizeof(SoundPlayingInfo) == 0x16, "gSoundPlayingInfo GBA layout drift");
_Static_assert(offsetof(SoundPlayingInfo, currentBgm) == 0x14, "gSoundPlayingInfo BGM offset drift");
_Static_assert(sizeof(gMapDataTopSpecial) == 0x8000, "gMapDataTopSpecial host extent drift");
_Static_assert(0x0aa40u + 0x4acu <= 0x0af00u, "gSave reviewed extent overlaps map data");
_Static_assert(0x0aa40u + 0x4c0u == 0x0af00u, "gSave filler/map data boundary drift");
_Static_assert(0x0af00u + 0x4000u == 0x0ef00u, "gMapDataTopSpecial/gUnk_02006F00 boundary drift");
_Static_assert(offsetof(ScriptExecutionContext, scriptInstructionPointer) == 0x00,
               "ScriptExecutionContext pointer host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, intVariable) == 0x08,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, postScriptActions) == 0x0c,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, unk_0C) == 0x10,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, wait) == 0x14,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, condition) == 0x18,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, x) == 0x20,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(offsetof(ScriptExecutionContext, y) == 0x24,
               "ScriptExecutionContext scalar host offset drift");
_Static_assert(0x3e570u + 32u * 0x24u == 0x3e9f0u,
               "gScriptExecutionContextArray GBA extent drift");

typedef struct {
    uint8_t bytes[TMC_RA_SNAPSHOT_BYTES];
    uint8_t provenance[TMC_RA_SNAPSHOT_BYTES];
    uint8_t validated[TMC_RA_SNAPSHOT_BYTES];
    uint32_t generation;
} TmcRaSnapshot;

static TmcRaSnapshot sSnapshots[2];
static unsigned sPublished;
static TmcRaReadAudit sAudit;
static uint8_t sRequestedCoverage[TMC_RA_SNAPSHOT_BYTES];
static uint32_t sRequestedCoverageBytes;
static uint32_t sRequestedCoverageOutOfRangeBytes;
#ifdef TMC_RA_MEMORY_TEST
static uint32_t sRequestedCoverageResetCount;
#endif

static bool IsRange(uint32_t offset, size_t bytes) {
    return offset <= TMC_RA_SNAPSHOT_BYTES && bytes <= TMC_RA_SNAPSHOT_BYTES - offset;
}

static void Mark(uint8_t* provenance, uint8_t* validated, uint32_t offset, size_t bytes,
                 TmcRaProvenance source, bool is_valid) {
    memset(provenance + offset, source, bytes);
    memset(validated + offset, is_valid ? 1 : 0, bytes);
}

static void InvalidateFrameDivergences(TmcRaSnapshot* snapshot) {
    /* These bytes are synchronization state, not stable post-frame gameplay
     * state. The native and mGBA capture loops observe the VBlank handshake
     * at different points around the same interrupt. */
    const uint32_t offsets[] = { 0x00fdcu, 0x00ff7u, 0x01000u, 0x0100cu };

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        Mark(snapshot->provenance, snapshot->validated, offsets[i], 1, TMC_RA_PROVENANCE_INVALID, false);
}

static void AuditAdd(uint32_t* value, size_t add) {
    if (add >= UINT32_MAX - *value) {
        *value = UINT32_MAX;
    } else {
        *value += (uint32_t)add;
    }
}

static void AuditInvalid(size_t bytes) {
    AuditAdd(&sAudit.invalid_requested_ranges, 1);
    AuditAdd(&sAudit.invalid_requested_bytes, bytes);
}

static void MarkRequestedCoverage(uint32_t offset, size_t bytes) {
    if (!IsRange(offset, bytes)) {
        AuditAdd(&sRequestedCoverageOutOfRangeBytes, bytes);
        return;
    }
    for (size_t i = 0; i < bytes; ++i) {
        if (!sRequestedCoverage[offset + i]) {
            sRequestedCoverage[offset + i] = 1;
            AuditAdd(&sRequestedCoverageBytes, 1);
        }
    }
}

static bool PackBytes(TmcRaSnapshot* snapshot, uint32_t offset, const void* value, size_t bytes) {
    if (value == NULL || !IsRange(offset, bytes))
        return false;
    memcpy(snapshot->bytes + offset, value, bytes);
    Mark(snapshot->provenance, snapshot->validated, offset, bytes, TMC_RA_PROVENANCE_EXPLICIT, true);
    return true;
}

static bool PackU8(TmcRaSnapshot* snapshot, uint32_t offset, uint8_t value) {
    return TmcRaMemory_WriteU8(snapshot->bytes, snapshot->provenance, snapshot->validated,
                               sizeof(snapshot->bytes), offset, value, TMC_RA_PROVENANCE_EXPLICIT);
}

static bool PackU16(TmcRaSnapshot* snapshot, uint32_t offset, uint16_t value) {
    return TmcRaMemory_WriteU16Le(snapshot->bytes, snapshot->provenance, snapshot->validated,
                                  sizeof(snapshot->bytes), offset, value, TMC_RA_PROVENANCE_EXPLICIT);
}

static bool PackU32(TmcRaSnapshot* snapshot, uint32_t offset, uint32_t value) {
    return TmcRaMemory_WriteU32Le(snapshot->bytes, snapshot->provenance, snapshot->validated,
                                  sizeof(snapshot->bytes), offset, value, TMC_RA_PROVENANCE_EXPLICIT);
}

static bool PackPointer(TmcRaSnapshot* snapshot, uint32_t offset, const void* pointer) {
    uint32_t gba_virtual = 0;
    const TmcRaFixedGlobal fixed_globals[] = {
        { &gPlayerEntity, 0x03001160u, sizeof(gPlayerEntity), 0x88u, 0, 0 },
        { gAuxPlayerEntities, 0x030011e8u, sizeof(gAuxPlayerEntities), MAX_AUX_PLAYER_ENTITIES * 0x88u,
          sizeof(gAuxPlayerEntities[0]), 0x88u },
        { gEntityLists, 0x03003d70u, sizeof(gEntityLists), 9u * 0x08u, sizeof(gEntityLists[0]), 0x08u },
        { &gPlayerState, 0x03003f80u, sizeof(gPlayerState), 0xb0u, 0, 0 },
        { gActiveItems, 0x03000b80u, sizeof(gActiveItems), MAX_ACTIVE_ITEMS * 0x1cu,
          sizeof(gActiveItems[0]), 0x1cu },
        { gArea.roomResInfos, 0x02033ad0u, MAX_ROOMS * 0x38u, MAX_ROOMS * 0x20u, 0x38u, 0x20u },
        { &gArea.currentRoomInfo, 0x020342d0u, 0x38u, 0x20u, 0x38u, 0x20u },
    };
    const bool translated = TmcRaMemory_TranslateHostPointer(
        pointer, gEntities, sizeof(gEntities), sizeof(gEntities[0]), 0x88u,
        fixed_globals, sizeof(fixed_globals) / sizeof(fixed_globals[0]), &gba_virtual, NULL);

    return TmcRaMemory_WriteTranslatedPointerLe(
        snapshot->bytes, snapshot->provenance, snapshot->validated, sizeof(snapshot->bytes), offset,
        gba_virtual, translated, TMC_RA_PROVENANCE_EXPLICIT);
}

static void PackEntity(TmcRaSnapshot* snapshot, uint32_t offset, const Entity* value) {
    PackPointer(snapshot, offset + 0x00, value->prev);
    PackPointer(snapshot, offset + 0x04, value->next);
    PackBytes(snapshot, offset + 0x08, &value->kind, 0x40);
    PackPointer(snapshot, offset + 0x48, value->hitbox);
    PackPointer(snapshot, offset + 0x4c, value->contactedEntity);
    PackPointer(snapshot, offset + 0x50, value->parent);
    PackPointer(snapshot, offset + 0x54, value->child);
    PackBytes(snapshot, offset + 0x58, &value->animIndex, 0x04);
    PackPointer(snapshot, offset + 0x5c, value->animPtr);
    PackBytes(snapshot, offset + 0x60, &value->spriteVramOffset, 0x04);
    PackPointer(snapshot, offset + 0x64, value->myHeap);
}

static void PackGenericEntity(TmcRaSnapshot* snapshot, uint32_t offset, const GenericEntity* value) {
    PackEntity(snapshot, offset, &value->base);
    PackBytes(snapshot, offset + 0x68, &value->field_0x68, 0x1c);
    /*
     * PC_PORT overlays the original final four bytes with an eight-byte
     * scriptContext pointer. There is no proven GBA reconstruction for it.
     */
}

static void PackPlayerEntity(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x01160u;
    PackEntity(snapshot, offset, &gPlayerEntity.base);
    PackU32(snapshot, offset + 0x68, gPlayerEntity.unk_68.WORD_U);
    PackU8(snapshot, offset + 0x6c, gPlayerEntity.unk_6c);
    PackU8(snapshot, offset + 0x6d, gPlayerEntity.unk_6d);
    PackU8(snapshot, offset + 0x6e, gPlayerEntity.unk_6e);
    PackU8(snapshot, offset + 0x6f, gPlayerEntity.unk_6f);
    PackPointer(snapshot, offset + 0x70, gPlayerEntity.pulledJarEntity);
    PackPointer(snapshot, offset + 0x74, gPlayerEntity.carriedEntity);
    PackU8(snapshot, offset + 0x78, gPlayerEntity.unk_78);
    PackU8(snapshot, offset + 0x79, gPlayerEntity.unk_79);
    PackU16(snapshot, offset + 0x7a, gPlayerEntity.unk_7a);
    PackU32(snapshot, offset + 0x7c, gPlayerEntity.unk_7c.WORD_U);
    PackU32(snapshot, offset + 0x80, gPlayerEntity.unk_80.WORD_U);
    PackU32(snapshot, offset + 0x84, gPlayerEntity.unk_84.WORD_U);
}

static void PackEntityPools(TmcRaSnapshot* snapshot) {
    for (unsigned i = 0; i < MAX_AUX_PLAYER_ENTITIES; ++i) {
        PackGenericEntity(snapshot, 0x011e8u + i * 0x88u, &gAuxPlayerEntities[i]);
    }
    for (unsigned i = 0; i < MAX_ENTITIES; ++i) {
        PackGenericEntity(snapshot, 0x015a0u + i * 0x88u, &gEntities[i]);
    }
}

static void PackEntityLists(TmcRaSnapshot* snapshot) {
    for (unsigned i = 0; i < 9; ++i) {
        const uint32_t offset = 0x03d70u + i * 0x08u;
        PackPointer(snapshot, offset + 0x00, gEntityLists[i].last);
        PackPointer(snapshot, offset + 0x04, gEntityLists[i].first);
    }
}

static void PackPlayerState(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x03f80u;
    PackBytes(snapshot, offset + 0x00, &gPlayerState.prevAnim, 0x2c);
    PackPointer(snapshot, offset + 0x2c, gPlayerState.item);
    PackBytes(snapshot, offset + 0x30, &gPlayerState.flags, 0x54);
    PackPointer(snapshot, offset + 0x84, gPlayerState.lilypad);
    PackBytes(snapshot, offset + 0x88, &gPlayerState.field_0x88, 0x08);
    PackU16(snapshot, offset + 0x90, gPlayerState.playerInput.heldInput);
    PackU16(snapshot, offset + 0x92, gPlayerState.playerInput.newInput);
    PackU32(snapshot, offset + 0x94, gPlayerState.playerInput.unused);
    PackU16(snapshot, offset + 0x98, gPlayerState.playerInput.playerMacroWaiting);
    PackU16(snapshot, offset + 0x9a, gPlayerState.playerInput.playerMacroHeldKeys);
    PackPointer(snapshot, offset + 0x9c, gPlayerState.playerInput.playerMacro);
    PackBytes(snapshot, offset + 0xa0, &gPlayerState.chargeState, 0x08);
    PackBytes(snapshot, offset + 0xa8, &gPlayerState.framestate, 0x08);
}

static void PackActiveItems(TmcRaSnapshot* snapshot) {
    for (unsigned i = 0; i < MAX_ACTIVE_ITEMS; ++i) {
        const uint32_t offset = 0x00b80u + i * 0x1cu;
        PackBytes(snapshot, offset, &gActiveItems[i].field_0x0, 0x18);
        PackPointer(snapshot, offset + 0x18, gActiveItems[i].field_0x18);
    }
}

static void PackPlayerRoomStatus(TmcRaSnapshot* snapshot, uint32_t offset, const PlayerRoomStatus* value) {
    PackU8(snapshot, offset + 0x00, value->area_next);
    PackU8(snapshot, offset + 0x01, value->room_next);
    PackU8(snapshot, offset + 0x02, value->start_anim);
    PackU8(snapshot, offset + 0x03, value->spawn_type);
    PackU16(snapshot, offset + 0x04, (uint16_t)value->start_pos_x);
    PackU16(snapshot, offset + 0x06, (uint16_t)value->start_pos_y);
    PackU8(snapshot, offset + 0x08, value->layer);
    PackU8(snapshot, offset + 0x0a, value->dungeon_area);
    PackU8(snapshot, offset + 0x0b, value->dungeon_room);
    PackU16(snapshot, offset + 0x0c, (uint16_t)value->dungeon_x);
    PackU16(snapshot, offset + 0x0e, (uint16_t)value->dungeon_y);
    PackU16(snapshot, offset + 0x10, value->dungeon_map_x);
    PackU16(snapshot, offset + 0x12, value->dungeon_map_y);
    PackU16(snapshot, offset + 0x14, value->overworld_map_x);
    PackU16(snapshot, offset + 0x16, value->overworld_map_y);
}

static void PackStats(TmcRaSnapshot* snapshot, uint32_t offset, const Stats* value) {
    PackBytes(snapshot, offset, value, offsetof(Stats, filler14));
    PackU16(snapshot, offset + offsetof(Stats, rupees), value->rupees);
    PackU16(snapshot, offset + offsetof(Stats, shells), value->shells);
    PackU16(snapshot, offset + offsetof(Stats, charmTimer), value->charmTimer);
    PackU16(snapshot, offset + offsetof(Stats, picolyteTimer), value->picolyteTimer);
    PackU16(snapshot, offset + offsetof(Stats, effectTimer), value->effectTimer);
}

static void PackKinstones(TmcRaSnapshot* snapshot, uint32_t offset, const KinstoneSave* value) {
    PackU8(snapshot, offset + offsetof(KinstoneSave, didAllFusions), value->didAllFusions);
    PackU8(snapshot, offset + offsetof(KinstoneSave, fusedCount), value->fusedCount);
    PackBytes(snapshot, offset + offsetof(KinstoneSave, types), value->types, sizeof(value->types));
    PackBytes(snapshot, offset + offsetof(KinstoneSave, amounts), value->amounts, sizeof(value->amounts));
    PackBytes(snapshot, offset + offsetof(KinstoneSave, fuserProgress), value->fuserProgress,
              sizeof(value->fuserProgress));
    PackBytes(snapshot, offset + offsetof(KinstoneSave, fuserOffers), value->fuserOffers,
              sizeof(value->fuserOffers));
    PackBytes(snapshot, offset + offsetof(KinstoneSave, fusedKinstones), value->fusedKinstones,
              sizeof(value->fusedKinstones));
    PackBytes(snapshot, offset + offsetof(KinstoneSave, fusionUnmarked), value->fusionUnmarked,
              sizeof(value->fusionUnmarked));
}

static void PackSave(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x0aa40u;
    PackU8(snapshot, offset + 0x000, gSave.invalid);
    PackU8(snapshot, offset + 0x001, gSave.initialized);
    PackU8(snapshot, offset + 0x002, gSave.msg_speed);
    PackU8(snapshot, offset + 0x003, gSave.brightness);
    PackU8(snapshot, offset + 0x006, gSave.saw_staffroll);
    PackU8(snapshot, offset + 0x007, gSave.dws_barrel_state);
    PackU8(snapshot, offset + 0x008, gSave.global_progress);
    PackU8(snapshot, offset + 0x009, gSave.available_figurines);
    PackU16(snapshot, offset + 0x020, gSave.map_hints);
    PackU32(snapshot, offset + 0x040, gSave.windcrests);
    PackU32(snapshot, offset + 0x050, gSave.enemies_killed);
    PackU32(snapshot, offset + 0x05c, gSave.items_bought);
    PackBytes(snapshot, offset + 0x060, gSave.areaVisitFlags, sizeof(gSave.areaVisitFlags));
    PackBytes(snapshot, offset + 0x080, gSave.name, sizeof(gSave.name));
    PackPlayerRoomStatus(snapshot, offset + 0x088, &gSave.saved_status);
    PackStats(snapshot, offset + 0x0a8, &gSave.stats);
    PackBytes(snapshot, offset + 0x0ce, gSave.figurines, sizeof(gSave.figurines));
    PackBytes(snapshot, offset + 0x0f2, gSave.inventory, sizeof(gSave.inventory));
    PackKinstones(snapshot, offset + 0x114, &gSave.kinstones);
    /* The host tail is shifted by one byte; preserve the documented GBA offsets. */
    PackBytes(snapshot, offset + 0x25c, gSave.flags, sizeof(gSave.flags));
    PackBytes(snapshot, offset + 0x45c, gSave.dungeonKeys, sizeof(gSave.dungeonKeys));
    PackBytes(snapshot, offset + 0x46c, gSave.dungeonItems, sizeof(gSave.dungeonItems));
    PackBytes(snapshot, offset + 0x47c, gSave.dungeonWarps, sizeof(gSave.dungeonWarps));
    PackU32(snapshot, offset + 0x48c, gSave.darknut_timer);
    PackU32(snapshot, offset + 0x490, gSave.drug_kill_count);
    PackU32(snapshot, offset + 0x494, gSave.biggoron_timer);
    PackU32(snapshot, offset + 0x498, gSave.vaati_timer);
    PackU32(snapshot, offset + 0x49c, gSave.timer4);
    PackU32(snapshot, offset + 0x4a0, gSave.timer5);
    PackU32(snapshot, offset + 0x4a4, gSave.timer6);
    PackU32(snapshot, offset + 0x4a8, gSave.demo_timer);
}

static void PackMessage(TmcRaSnapshot* snapshot) {
    PackBytes(snapshot, 0x08050u, &gMessage, 0x20);
}

static void PackMenu(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x08080u;

    /* FileSelectState is private to fileselect.c; STATE_NONE is zero. */
    if (gMain.task == TASK_TITLE || (gMain.task == TASK_FILE_SELECT && gUI.lastState == 0)) {
        PackBytes(snapshot, offset, _gMenuSharedStorage, 0x30);
    } else {
        PackBytes(snapshot, offset, &_gMenuSharedStorage[0], 0x0c);
        PackPointer(snapshot, offset + 0x0c, gMenu.field_0xc);
        PackBytes(snapshot, offset + 0x10, &gGenericMenu.unk10, 0x04);
        PackBytes(snapshot, offset + 0x14, &gGenericMenu.unk14, 0x04);
        PackBytes(snapshot, offset + 0x18, &gGenericMenu.unk18, 0x04);
        PackBytes(snapshot, offset + 0x1c, &gGenericMenu.unk1c, 0x06);
        PackBytes(snapshot, offset + 0x22, gGenericMenu.filler22, 0x06);
        PackBytes(snapshot, offset + 0x28, &gGenericMenu.unk28, 0x08);
    }
}

static void PackUIElement(TmcRaSnapshot* snapshot, uint32_t offset, const UIElement* value) {
    PackBytes(snapshot, offset, value, 0x14);
    PackPointer(snapshot, offset + 0x14, value->framePtr);
    PackBytes(snapshot, offset + 0x18, &value->unk_18, 0x04);
    PackPointer(snapshot, offset + 0x1c, value->firstTile);
}

static void PackHUD(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x12f00u;

    PackBytes(snapshot, offset, &gHUD.unk_0, 0x34);
    for (unsigned i = 0; i < MAX_UI_ELEMENTS; ++i) {
        PackUIElement(snapshot, offset + 0x34u + i * 0x20u, &gHUD.elements[i]);
    }
}

static void PackGfxSlot(TmcRaSnapshot* snapshot, uint32_t offset, const GfxSlot* value) {
    PackBytes(snapshot, offset, value, 0x08);
    PackPointer(snapshot, offset + 0x08, value->palettePointer);
}

static void PackUI(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x3aec0u;

    PackBytes(snapshot, offset, &gUI.nextToLoad, 0x10);
    PackPointer(snapshot, offset + 0x10, gUI.currentRoomProperties);
    PackPointer(snapshot, offset + 0x14, gUI.mapBottomBgSettings);
    PackPointer(snapshot, offset + 0x18, gUI.mapTopBgSettings);
    PackBytes(snapshot, offset + 0x1c, &gUI.roomControls, 0x30);
    PackPointer(snapshot, offset + 0x4c, gUI.roomControls.camera_target);
    /* gUI.roomControls.tileSet is a native pointer-shaped u32 and remains invalid. */
    for (unsigned i = 0; i < MAX_GFX_SLOTS; ++i) {
        PackGfxSlot(snapshot, offset + 0x54u + i * 0x0cu, &gUI.gfxSlotList.slots[i]);
    }
    PackBytes(snapshot, offset + 0x268, gUI.palettes, 0x40);
    PackBytes(snapshot, offset + 0x2a8, gUI.unk_2a8, 0x100);
    /* gUI.activeScriptInfo and its GBA alignment tail are outside P2B3a. */
}

static void PackRoomControls(TmcRaSnapshot* snapshot) {
    uint32_t gba_virtual = 0;
    const TmcRaFixedGlobal fixed_globals[] = {
        { &gPlayerEntity, 0x03001160u, sizeof(gPlayerEntity), 0x88u, 0, 0 },
    };
    bool translated = TmcRaMemory_TranslateHostPointer(
        gRoomControls.camera_target, gEntities, sizeof(gEntities), sizeof(gEntities[0]), 0x88u,
        fixed_globals, sizeof(fixed_globals) / sizeof(fixed_globals[0]), &gba_virtual, NULL);

    PackBytes(snapshot, 0x00bf0u, &gRoomControls, offsetof(RoomControls, camera_target));
    TmcRaMemory_WriteTranslatedPointerLe(snapshot->bytes, snapshot->provenance, snapshot->validated,
                                         sizeof(snapshot->bytes), 0x00c20u, gba_virtual, translated,
                                         TMC_RA_PROVENANCE_EXPLICIT);
}

static void PackMapLayer(TmcRaSnapshot* snapshot, uint32_t offset, const MapLayer* value) {
    /* GBA MapLayer is 0xc004 bytes; host pointer width shifts every array by four bytes. */
    PackPointer(snapshot, offset + 0x0000, value->bgSettings);
    PackBytes(snapshot, offset + 0x0004, value->mapData, 0x2000);
    PackBytes(snapshot, offset + 0x2004, value->collisionData, 0x1000);
    PackBytes(snapshot, offset + 0x3004, value->mapDataOriginal, 0x2000);
    PackBytes(snapshot, offset + 0x5004, value->tileTypes, 0x1000);
    PackBytes(snapshot, offset + 0x6004, value->tileIndices, 0x1000);
    PackBytes(snapshot, offset + 0x7004, value->subTiles, 0x4000);
    PackBytes(snapshot, offset + 0xb004, value->actTiles, 0x1000);
}

static void PackRoomResInfo(TmcRaSnapshot* snapshot, uint32_t offset, const RoomResInfo* value) {
    PackU16(snapshot, offset + 0x00, value->pixel_width);
    PackU16(snapshot, offset + 0x02, value->pixel_height);
    PackU16(snapshot, offset + 0x04, value->map_x);
    PackU16(snapshot, offset + 0x06, value->map_y);
    PackPointer(snapshot, offset + 0x08, value->tileSet);
    PackPointer(snapshot, offset + 0x0c, value->map);
    PackPointer(snapshot, offset + 0x10, value->tiles);
    PackPointer(snapshot, offset + 0x14, value->bg_anim);
    PackPointer(snapshot, offset + 0x18, value->exits);
    PackPointer(snapshot, offset + 0x1c, value->properties);
}

static void PackArea(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x3ba90u;

    PackU8(snapshot, offset + 0x00, gArea.areaMetadata);
    PackU8(snapshot, offset + 0x01, gArea.locationIndex);
    PackU8(snapshot, offset + 0x02, gArea.unk);
    PackU8(snapshot, offset + 0x03, gArea.dungeon_idx);
    PackU16(snapshot, offset + 0x04, gArea.localFlagOffset);
    PackU8(snapshot, offset + 0x06, gArea.flag_bank);
    PackU16(snapshot, offset + 0x0a, gArea.lightLevel);
    PackU8(snapshot, offset + 0x0c, gArea.lightType);
    PackU8(snapshot, offset + 0x0d,
           (gArea.unk_0c_0 & 0x01u) | ((gArea.unk_0c_1 & 0x07u) << 1) | ((gArea.unk_0c_4 & 0x0fu) << 4));
    PackU16(snapshot, offset + 0x10, gArea.field_0x10);
    PackU16(snapshot, offset + 0x12, gArea.portal_x);
    PackU16(snapshot, offset + 0x14, gArea.portal_y);
    PackU8(snapshot, offset + 0x16, gArea.portal_exit_dir);
    PackU8(snapshot, offset + 0x17, gArea.portal_type);
    PackU8(snapshot, offset + 0x18, gArea.portal_mode);
    PackU8(snapshot, offset + 0x19, gArea.portal_in_use);
    PackU8(snapshot, offset + 0x1a, gArea.portal_timer);
    PackBytes(snapshot, offset + 0x28, &gArea.unk28, 0x14);
    for (unsigned i = 0; i < MAX_ROOMS; ++i)
        PackRoomResInfo(snapshot, offset + 0x40u + i * 0x20u, &gArea.roomResInfos[i]);
    PackRoomResInfo(snapshot, offset + 0x840, &gArea.currentRoomInfo);
    PackPointer(snapshot, offset + 0x860, gArea.pCurrentRoomInfo);
    PackU32(snapshot, offset + 0x864, gArea.bgm);
    /*
     * gUnk_020342F8 begins at +0x868 and aliases gArea's native tail on GBA.
     * Its overlap prevents a contiguous reviewed gArea extent.
     */
}

static void PackRoomVars(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x3c350u;

    PackBytes(snapshot, offset + 0x00, &gRoomVars.didEnterScrolling, 0x0b);
    PackU16(snapshot, offset + 0x0c, (uint16_t)gRoomVars.lightLevel);
    PackU16(snapshot, offset + 0x0e, gRoomVars.tileEntityCount);
    PackBytes(snapshot, offset + 0x10, gRoomVars.graphicsGroups, 0x04);
    PackBytes(snapshot, offset + 0x14, gRoomVars.flags, 0x34);
    PackBytes(snapshot, offset + 0x48, &gRoomVars.currentAreaDroptable, 0x20);
    PackU32(snapshot, offset + 0x68, gRoomVars.animFlags);
    for (unsigned i = 0; i < 8; ++i) {
        PackPointer(snapshot, offset + 0x6cu + i * 0x04u, gRoomVars.properties[i]);
        PackPointer(snapshot, offset + 0x8cu + i * 0x04u, gRoomVars.entityRails[i]);
        PackPointer(snapshot, offset + 0xacu + i * 0x04u, gRoomVars.puzzleEntities[i]);
    }
}

static void PackOamData(TmcRaSnapshot* snapshot, uint32_t offset, const struct OamData* value) {
    const uint16_t attr0 = (uint16_t)((value->y & 0xffu) | ((value->affineMode & 0x03u) << 8) |
                                      ((value->objMode & 0x03u) << 10) | ((value->mosaic & 0x01u) << 12) |
                                      ((value->bpp & 0x01u) << 13) | ((value->shape & 0x03u) << 14));
    const uint16_t attr1 = (uint16_t)((value->x & 0x1ffu) | ((value->matrixNum & 0x1fu) << 9) |
                                      ((value->size & 0x03u) << 14));
    const uint16_t attr2 =
        (uint16_t)((value->tileNum & 0x3ffu) | ((value->priority & 0x03u) << 10) | ((value->paletteNum & 0x0fu) << 12));

    PackU16(snapshot, offset + 0x00, attr0);
    PackU16(snapshot, offset + 0x02, attr1);
    PackU16(snapshot, offset + 0x04, attr2);
    PackU16(snapshot, offset + 0x06, value->affineParam);
}

static void PackOAMControls(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x00000u;

    PackU8(snapshot, offset + 0x00, gOAMControls.field_0x0);
    PackU8(snapshot, offset + 0x01, gOAMControls.field_0x1);
    PackU8(snapshot, offset + 0x02, gOAMControls.spritesOffset);
    PackU8(snapshot, offset + 0x03, gOAMControls.updated);
    PackU16(snapshot, offset + 0x04, gOAMControls._4);
    PackU16(snapshot, offset + 0x06, gOAMControls._6);
    PackBytes(snapshot, offset + 0x08, gOAMControls._0, 0x18);
    for (unsigned i = 0; i < 0x80; ++i)
        PackOamData(snapshot, offset + 0x20u + i * 0x08u, &gOAMControls.oam[i]);
}

static void PackBgSettings(TmcRaSnapshot* snapshot, uint32_t offset, const BgSettings* value) {
    PackU16(snapshot, offset + 0x00, value->control);
    PackU16(snapshot, offset + 0x02, value->xOffset);
    PackU16(snapshot, offset + 0x04, value->yOffset);
    PackU16(snapshot, offset + 0x06, value->updated);
    PackPointer(snapshot, offset + 0x08, value->subTileMap);
}

static void PackBgAffSettings(TmcRaSnapshot* snapshot, uint32_t offset, const BgAffSettings* value) {
    PackU16(snapshot, offset + 0x00, value->control);
    PackU16(snapshot, offset + 0x02, (uint16_t)value->xOffset);
    PackU16(snapshot, offset + 0x04, (uint16_t)value->yOffset);
    PackU16(snapshot, offset + 0x06, value->updated);
    PackPointer(snapshot, offset + 0x08, value->subTileMap);
}

static void PackBgTransformationSettings(TmcRaSnapshot* snapshot, uint32_t offset,
                                         const BgTransformationSettings* value) {
    PackU16(snapshot, offset + 0x00, value->dx);
    PackU16(snapshot, offset + 0x02, value->dmx);
    PackU16(snapshot, offset + 0x04, value->dy);
    PackU16(snapshot, offset + 0x06, value->dmy);
    PackU16(snapshot, offset + 0x08, value->xPointLeastSig);
    PackU16(snapshot, offset + 0x0a, value->xPointMostSig);
    PackU16(snapshot, offset + 0x0c, value->yPointLeastSig);
    PackU16(snapshot, offset + 0x0e, value->yPointMostSig);
}

static void PackBgControls(TmcRaSnapshot* snapshot, uint32_t offset, const BgControls* value) {
    PackBgTransformationSettings(snapshot, offset + 0x00, &value->bg2);
    PackBgTransformationSettings(snapshot, offset + 0x10, &value->bg3);
    PackU16(snapshot, offset + 0x20, value->window0HorizontalDimensions);
    PackU16(snapshot, offset + 0x22, value->window1HorizontalDimensions);
    PackU16(snapshot, offset + 0x24, value->window0VerticalDimensions);
    PackU16(snapshot, offset + 0x26, value->window1VerticalDimensions);
    PackU16(snapshot, offset + 0x28, value->windowInsideControl);
    PackU16(snapshot, offset + 0x2a, value->windowOutsideControl);
    PackU16(snapshot, offset + 0x2c, value->mosaicSize);
    PackU16(snapshot, offset + 0x2e, value->layerFXControl);
    PackU16(snapshot, offset + 0x30, value->alphaBlend);
    PackU16(snapshot, offset + 0x32, value->layerBrightness);
}

static void PackScreen(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x00f50u;

    PackU16(snapshot, offset + 0x00, gScreen.lcd.displayControl);
    PackU8(snapshot, offset + 0x02, gScreen.lcd.filler2[0]);
    PackU8(snapshot, offset + 0x03, gScreen.lcd.filler2[1]);
    PackU16(snapshot, offset + 0x04, gScreen.lcd.unk4);
    PackU16(snapshot, offset + 0x06, gScreen.lcd.displayControlMask);
    PackBgSettings(snapshot, offset + 0x08, &gScreen.bg0);
    PackBgSettings(snapshot, offset + 0x14, &gScreen.bg1);
    PackBgAffSettings(snapshot, offset + 0x20, &gScreen.bg2);
    PackBgAffSettings(snapshot, offset + 0x2c, &gScreen.bg3);
    PackBgControls(snapshot, offset + 0x38, &gScreen.controls);
    PackU8(snapshot, offset + 0x6c, gScreen.vBlankDMA.ready);
    PackU8(snapshot, offset + 0x6d, gScreen.vBlankDMA.readyBackup);
    PackU16(snapshot, offset + 0x6e, gScreen.vBlankDMA.unused);
    PackPointer(snapshot, offset + 0x70, gScreen.vBlankDMA.src);
    PackPointer(snapshot, offset + 0x74, gScreen.vBlankDMA.dest);
    PackU32(snapshot, offset + 0x78, gScreen.vBlankDMA.size);
}

static void PackBgAnimations(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x08cc0u;

    for (unsigned i = 0; i < MAX_BG_ANIMATIONS; ++i) {
        const BgAnimation* value = &gBgAnimations[i];
        const uint32_t entry_offset = offset + i * 0x08u;

        PackPointer(snapshot, entry_offset + 0x00, value->currentFrame);
        PackU16(snapshot, entry_offset + 0x04, value->unk_4);
        PackU16(snapshot, entry_offset + 0x06, value->timer);
    }
}

static void PackPauseMenuOptions(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x3c490u;

    PackU8(snapshot, offset + 0x00, gPauseMenuOptions.disabled);
    PackU8(snapshot, offset + 0x01, gPauseMenuOptions.screen);
}

static void PackMapDataTopSpecial(TmcRaSnapshot* snapshot) {
    const uint32_t offset = 0x0af00u;

    for (unsigned i = 0; i < 0x2000; ++i)
        PackU16(snapshot, offset + i * 2u, gMapDataTopSpecial[i]);
}

static void PackScriptExecutionContext(TmcRaSnapshot* snapshot, uint32_t offset,
                                       const ScriptExecutionContext* value) {
    PackPointer(snapshot, offset + 0x00, value->scriptInstructionPointer);
    PackU32(snapshot, offset + 0x04, value->intVariable);
    PackU32(snapshot, offset + 0x08, value->postScriptActions);
    PackBytes(snapshot, offset + 0x0c, value->unk_0C, 0x04);
    PackU16(snapshot, offset + 0x10, value->wait);
    PackU16(snapshot, offset + 0x12, value->unk_12);
    PackU32(snapshot, offset + 0x14, value->condition);
    PackU8(snapshot, offset + 0x18, value->unk_18);
    PackU8(snapshot, offset + 0x19, value->unk_19);
    PackU8(snapshot, offset + 0x1a, value->unk_1A);
    PackU8(snapshot, offset + 0x1b, value->unk_1B);
    PackU32(snapshot, offset + 0x1c, value->x.WORD_U);
    PackU32(snapshot, offset + 0x20, value->y.WORD_U);
}

static void PackScriptExecutionContextArray(TmcRaSnapshot* snapshot) {
    for (unsigned i = 0; i < 32; ++i)
        PackScriptExecutionContext(snapshot, 0x3e570u + i * 0x24u, &gScriptExecutionContextArray[i]);
}

static void PackNativeGlobals(TmcRaSnapshot* snapshot) {
    PackOAMControls(snapshot);
    PackActiveItems(snapshot);
    PackScreen(snapshot);
    PackBytes(snapshot, 0x00fd0u, &gFadeControl, 0x1au);
    PackBytes(snapshot, 0x00ff0u, &gInput, sizeof(gInput));
    PackBytes(snapshot, 0x01000u, &gMain, sizeof(gMain));
    InvalidateFrameDivergences(snapshot);
    PackBytes(snapshot, 0x010a0u, &gRoomTransition, sizeof(gRoomTransition));
    PackU32(snapshot, 0x01150u, gRand);
    PackPlayerEntity(snapshot);
    PackEntityPools(snapshot);
    PackEntityLists(snapshot);
    PackPlayerState(snapshot);
    PackRoomControls(snapshot);
    PackMessage(snapshot);
    PackMenu(snapshot);
    PackBgAnimations(snapshot);
    PackHUD(snapshot);
    PackUI(snapshot);
    PackBytes(snapshot, 0x2c050u, gRoomMemory, sizeof(RoomMemory) * 8);
    PackBytes(snapshot, 0x29ee0u, &gSoundPlayingInfo, sizeof(gSoundPlayingInfo));
    PackMapLayer(snapshot, 0x13650u, &gMapTop);
    PackMapLayer(snapshot, 0x2deb0u, &gMapBottom);
    PackArea(snapshot);
    PackRoomVars(snapshot);
    PackPauseMenuOptions(snapshot);
    PackSave(snapshot);
    /* The second host half aliases separate gUnk_02006F00 storage on GBA. */
    PackMapDataTopSpecial(snapshot);
    PackScriptExecutionContextArray(snapshot);
}

void TmcRaMemory_Publish(void) {
    const unsigned next = sPublished ^ 1u;
    TmcRaSnapshot* snapshot = &sSnapshots[next];

    memcpy(snapshot->bytes, gIwram, TMC_RA_IWRAM_BYTES);
    Mark(snapshot->provenance, snapshot->validated, 0, TMC_RA_IWRAM_BYTES,
         TMC_RA_PROVENANCE_RAW_IWRAM, false);
    memcpy(snapshot->bytes + TMC_RA_IWRAM_BYTES, gEwram, TMC_RA_EWRAM_BYTES);
    Mark(snapshot->provenance, snapshot->validated, TMC_RA_IWRAM_BYTES, TMC_RA_EWRAM_BYTES,
         TMC_RA_PROVENANCE_RAW_EWRAM, false);

    Port_Save_ReadEepromRaSnapshot(snapshot->bytes + TMC_RA_SAVE_OFFSET);
    memset(snapshot->bytes + TMC_RA_SAVE_OFFSET + PORT_SAVE_EEPROM_BYTES, 0,
           TMC_RA_SAVE_BYTES - PORT_SAVE_EEPROM_BYTES);
    Mark(snapshot->provenance, snapshot->validated, TMC_RA_SAVE_OFFSET, PORT_SAVE_EEPROM_BYTES,
         TMC_RA_PROVENANCE_SAVE, true);
    Mark(snapshot->provenance, snapshot->validated, TMC_RA_SAVE_OFFSET + PORT_SAVE_EEPROM_BYTES,
         TMC_RA_SAVE_BYTES - PORT_SAVE_EEPROM_BYTES, TMC_RA_PROVENANCE_INVALID, false);

    PackNativeGlobals(snapshot);
    snapshot->generation = sSnapshots[sPublished].generation + 1u;
    sPublished = next;
}

TmcRaSnapshotView TmcRaMemory_Current(void) {
    const TmcRaSnapshot* snapshot = &sSnapshots[sPublished];
    TmcRaSnapshotView view = {
        snapshot->bytes, snapshot->provenance, snapshot->validated, snapshot->generation, true
    };
    return view;
}

TmcRaFrameView TmcRaMemory_CurrentFrameView(void) {
    return (TmcRaFrameView) {
        TmcRaMemory_Current(), TmcRaMemory_RequestedCoverage(), TmcRaMemory_ReadAudit()
    };
}

const char* TmcRaMemory_ManifestHash(void) {
    return TMC_RA_MEMORY_MANIFEST_SHA256;
}

void TmcRaMemory_ResetAudit(void) {
    memset(&sAudit, 0, sizeof(sAudit));
}

TmcRaReadAudit TmcRaMemory_ReadAudit(void) {
    return sAudit;
}

void TmcRaMemory_ResetRequestedCoverage(void) {
    memset(sRequestedCoverage, 0, sizeof(sRequestedCoverage));
    sRequestedCoverageBytes = 0;
    sRequestedCoverageOutOfRangeBytes = 0;
#ifdef TMC_RA_MEMORY_TEST
    ++sRequestedCoverageResetCount;
#endif
}

TmcRaRequestedCoverage TmcRaMemory_RequestedCoverage(void) {
    return (TmcRaRequestedCoverage) {
        sRequestedCoverage, sRequestedCoverageBytes, sRequestedCoverageOutOfRangeBytes
    };
}

#ifdef TMC_RA_MEMORY_TEST
void TmcRaMemory_TestResetCoverageResetCount(void) {
    sRequestedCoverageResetCount = 0;
}

uint32_t TmcRaMemory_TestCoverageResetCount(void) {
    return sRequestedCoverageResetCount;
}
#endif

uint8_t TmcRaMemory_Read(uint32_t ra_physical, bool* valid) {
    AuditAdd(&sAudit.requested_ranges, 1);
    AuditAdd(&sAudit.requested_bytes, 1);
    MarkRequestedCoverage(ra_physical, 1);
    if (ra_physical < TMC_RA_SNAPSHOT_BYTES) {
        const TmcRaSnapshot* snapshot = &sSnapshots[sPublished];
        if (snapshot->validated[ra_physical]) {
            if (valid)
                *valid = true;
            return snapshot->bytes[ra_physical];
        }
    }
    AuditInvalid(1);
    if (valid)
        *valid = false;
    return 0;
}

bool TmcRaMemory_ReadBlock(uint32_t ra_physical, uint8_t* out, size_t out_capacity, size_t bytes) {
    bool all_valid = true;
    AuditAdd(&sAudit.requested_ranges, 1);
    AuditAdd(&sAudit.requested_bytes, bytes);
    MarkRequestedCoverage(ra_physical, bytes);
    if (out == NULL || out_capacity < bytes || !IsRange(ra_physical, bytes)) {
        if (out && out_capacity >= bytes)
            memset(out, 0, bytes);
        AuditInvalid(bytes);
        return false;
    }
    for (size_t i = 0; i < bytes; ++i) {
        const TmcRaSnapshot* snapshot = &sSnapshots[sPublished];
        const bool valid = snapshot->validated[ra_physical + i] != 0;
        out[i] = valid ? snapshot->bytes[ra_physical + i] : 0;
        if (!valid) {
            all_valid = false;
            AuditAdd(&sAudit.invalid_requested_bytes, 1);
        }
    }
    if (!all_valid) {
        AuditAdd(&sAudit.invalid_requested_ranges, 1);
    }
    return all_valid;
}

bool TmcRaMemory_OverlayExplicit(uint32_t offset, const uint8_t* bytes, size_t bytes_count) {
    TmcRaSnapshot* snapshot = &sSnapshots[sPublished];
    if (bytes == NULL || !IsRange(offset, bytes_count))
        return false;
    memcpy(snapshot->bytes + offset, bytes, bytes_count);
    Mark(snapshot->provenance, snapshot->validated, offset, bytes_count, TMC_RA_PROVENANCE_EXPLICIT, true);
    return true;
}

bool TmcRaMemory_VirtualToPhysical(uint32_t gba_virtual, uint32_t* ra_physical) {
    if (ra_physical == NULL)
        return false;
    if (gba_virtual >= 0x03000000u && gba_virtual <= 0x03007fffu) {
        *ra_physical = gba_virtual - 0x03000000u;
        return true;
    }
    if (gba_virtual >= 0x02000000u && gba_virtual <= 0x0203ffffu) {
        *ra_physical = TMC_RA_IWRAM_BYTES + gba_virtual - 0x02000000u;
        return true;
    }
    if (gba_virtual >= 0x0e000000u && gba_virtual <= 0x0e00ffffu) {
        *ra_physical = TMC_RA_SAVE_OFFSET + gba_virtual - 0x0e000000u;
        return true;
    }
    return false;
}

bool TmcRaMemory_WriteU8(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                         uint32_t offset, uint8_t value, TmcRaProvenance source) {
    if (bytes && provenance && validated && offset < capacity) {
        bytes[offset] = value;
        provenance[offset] = source;
        validated[offset] = source == TMC_RA_PROVENANCE_EXPLICIT;
        return true;
    }
    return false;
}

bool TmcRaMemory_WriteU16Le(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                            uint32_t offset, uint16_t value, TmcRaProvenance source) {
    if (bytes == NULL || provenance == NULL || validated == NULL || offset > capacity || capacity - offset < 2)
        return false;
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
    provenance[offset] = provenance[offset + 1] = source;
    validated[offset] = validated[offset + 1] = source == TMC_RA_PROVENANCE_EXPLICIT;
    return true;
}

bool TmcRaMemory_WriteU32Le(uint8_t* bytes, uint8_t* provenance, uint8_t* validated, size_t capacity,
                            uint32_t offset, uint32_t value, TmcRaProvenance source) {
    if (bytes == NULL || provenance == NULL || validated == NULL || offset > capacity || capacity - offset < 4)
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        bytes[offset + i] = (uint8_t)(value >> (8 * i));
        provenance[offset + i] = source;
        validated[offset + i] = source == TMC_RA_PROVENANCE_EXPLICIT;
    }
    return true;
}

bool TmcRaMemory_WriteTranslatedPointerLe(uint8_t* bytes, uint8_t* provenance, uint8_t* validated,
                                          size_t capacity, uint32_t offset, uint32_t gba_virtual,
                                          bool translated, TmcRaProvenance source) {
    if (translated) {
        return TmcRaMemory_WriteU32Le(bytes, provenance, validated, capacity, offset, gba_virtual, source);
    } else {
        /* An unknown non-null host pointer must never enter the snapshot. */
        return TmcRaMemory_WriteU32Le(bytes, provenance, validated, capacity, offset, 0,
                                      TMC_RA_PROVENANCE_INVALID);
    }
}

static bool PointerInRange(uintptr_t value, uintptr_t base, uint32_t bytes, uint32_t* offset) {
    if (value >= base && (uint64_t)(value - base) < bytes) {
        *offset = (uint32_t)(value - base);
        return true;
    }
    return false;
}

bool TmcRaMemory_TranslateHostPointer(const void* pointer, const void* entity_pool, uint32_t entity_pool_bytes,
                                      uint32_t host_entity_stride, uint32_t gba_entity_stride,
                                      const TmcRaFixedGlobal* fixed_globals,
                                      size_t fixed_global_count, uint32_t* gba_virtual,
                                      TmcRaPointerCategory* category) {
    const uintptr_t value = (uintptr_t)pointer;
    uint32_t offset;
    if (category)
        *category = TMC_RA_POINTER_UNKNOWN;
    if (gba_virtual == NULL || (fixed_global_count != 0 && fixed_globals == NULL))
        return false;
    if (pointer == NULL) {
        if (gba_virtual)
            *gba_virtual = 0;
        if (category)
            *category = TMC_RA_POINTER_NULL;
        return true;
    }
    if (PointerInRange(value, (uintptr_t)gEwram, sizeof(gEwram), &offset)) {
        *gba_virtual = 0x02000000u + offset;
        if (category) *category = TMC_RA_POINTER_RAW_EWRAM;
        return true;
    }
    if (PointerInRange(value, (uintptr_t)gIwram, sizeof(gIwram), &offset)) {
        *gba_virtual = 0x03000000u + offset;
        if (category) *category = TMC_RA_POINTER_RAW_IWRAM;
        return true;
    }
    if (gRomData && PointerInRange(value, (uintptr_t)gRomData, gRomSize, &offset)) {
        *gba_virtual = 0x08000000u + offset;
        if (category) *category = TMC_RA_POINTER_ROM;
        return true;
    }
    if (entity_pool && host_entity_stride && gba_entity_stride &&
        PointerInRange(value, (uintptr_t)entity_pool, entity_pool_bytes, &offset) &&
        offset % host_entity_stride == 0) {
        /* linker.ld places gEntities in IWRAM at 0x030015A0. */
        *gba_virtual = 0x030015a0u + (offset / host_entity_stride) * gba_entity_stride;
        if (category) *category = TMC_RA_POINTER_ENTITY_POOL;
        return true;
    }
    for (size_t i = 0; i < fixed_global_count; ++i) {
        if (fixed_globals[i].host &&
            PointerInRange(value, (uintptr_t)fixed_globals[i].host, fixed_globals[i].host_bytes, &offset)) {
            if (fixed_globals[i].host_stride || fixed_globals[i].gba_stride) {
                if (fixed_globals[i].host_stride == 0 || fixed_globals[i].gba_stride == 0 ||
                    offset % fixed_globals[i].host_stride != 0)
                    return false;
                offset = (offset / fixed_globals[i].host_stride) * fixed_globals[i].gba_stride;
            }
            if (offset >= fixed_globals[i].gba_bytes)
                return false;
            *gba_virtual = fixed_globals[i].gba_virtual + offset;
            if (category) *category = TMC_RA_POINTER_FIXED_GLOBAL;
            return true;
        }
    }
    return false;
}

#ifdef TMC_RA_MEMORY_TEST
void TmcRaMemory_TestSetAudit(TmcRaReadAudit audit) {
    sAudit = audit;
}
#endif
