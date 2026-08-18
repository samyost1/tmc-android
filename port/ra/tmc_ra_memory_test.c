#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "area.h"
#include "backgroundAnimations.h"
#include "common.h"
#include "fade.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "port_gba_mem.h"
#include "port_save.h"
#include "room.h"
#include "screen.h"
#include "save.h"
#include "script.h"
#include "sound.h"
#include "structures.h"
#include "tileMap.h"
#include "tmc_ra_memory.h"

u8 gIoMem[0x400], gEwram[0x40000], gIwram[0x8000], gVram[0x18000];
u16 gBgPltt[256], gObjPltt[256], gOamMem[0x200];
u8* gRomData;
u32 gRomSize;
Input gInput;
u32 gRand;
Main gMain;
FadeControl gFadeControl;
RoomControls gRoomControls;
RoomTransition gRoomTransition;
RoomMemory gRoomMemory[8];
SaveFile gSave;
PlayerEntity gPlayerEntity;
GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES];
GenericEntity gEntities[MAX_ENTITIES];
LinkedList gEntityLists[9];
PlayerState gPlayerState;
ItemBehavior gActiveItems[MAX_ACTIVE_ITEMS];
Message gMessage;
HUD gHUD;
UI gUI;
MapLayer gMapTop;
MapLayer gMapBottom;
Area gArea;
RoomVars gRoomVars;
OAMControls gOAMControls;
Screen gScreen;
BgAnimation gBgAnimations[MAX_BG_ANIMATIONS];
PauseMenuOptions gPauseMenuOptions;
SoundPlayingInfo gSoundPlayingInfo;
ScriptExecutionContext gScriptExecutionContextArray[0x20];
u16 gMapDataTopSpecial[0x4000];
u8 gUnk_02006F00[0x4000];
u8 gUnk_02034492[0x10];
u8 _gMenuSharedStorage[0x40];
void Port_LogRomAccess(u32 address, const char* caller) { (void)address; (void)caller; }
void Port_Save_ReadEepromRaSnapshot(uint8_t out[PORT_SAVE_EEPROM_BYTES]) {
    for (unsigned i = 0; i < PORT_SAVE_EEPROM_BYTES; ++i) out[i] = (uint8_t)i;
}

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", #x); fails++; } } while (0)

int main(void) {
    bool valid = false;
    uint32_t physical, virtual_address;
    TmcRaPointerCategory category;
    TmcRaSnapshotView first_view;
    uint8_t bytes[4], provenance[4], validated[4], explicit_bytes[2] = { 0xA5, 0x5A };
    uint8_t rom[4] = { 0 };
    uint8_t entities[16] = { 0 };
    const TmcRaFixedGlobal global = { entities, 0x03001160u, 12, 16, 0, 0 };
    const TmcRaFixedGlobal strided_global = { entities, 0x03002000u, 12, 9, 4, 3 };
    memset(gIwram, 0x11, sizeof(gIwram));
    memset(gEwram, 0x22, sizeof(gEwram));
    gMain.interruptFlag = 0xa1;
    gMain.task = 3;
    gMain.ticks = 0x1234;
    gInput.heldKeys = 0x5678;
    gInput.menuScrollTimer = 0x9a;
    gFadeControl.active = 0xa2;
    gFadeControl.mask = 0x89abcdef;
    gFadeControl.win_outside_cnt = 0x1357;
    gRand = 0x12345678;
    gRoomControls.reload_flags = 0xb3b4;
    gRoomControls.origin_x = 0x1234;
    gRoomControls.scroll_y = -2;
    gRoomControls.bg3OffsetY.WORD = 0x11223344;
    gRoomControls.camera_target = (Entity*)&gEntities[1];
    gRoomControls.tileSet = 0x10203040;
    gRoomTransition.frameCount = 0x11223344;
    gRoomTransition.player_status.start_pos_y = -3;
    gRoomTransition.armos_data.field_0xae = 0xbeef;
    gRoomMemory[0].area = 0x44;
    gRoomMemory[7].area = 0x12;
    gRoomMemory[7].enemyBits = 0x55667788;
    gSave.invalid = 1;
    gSave.global_progress = 0x55;
    gSave.map_hints = 0x1234;
    gSave.windcrests = 0x89abcdef;
    gSave.saved_status.area_next = 2;
    gSave.saved_status.start_pos_x = -4;
    gSave.stats.walletType = 3;
    gSave.stats.effect = 0x6b;
    gSave.stats.rupees = 0x4567;
    gSave.kinstones.didAllFusions = 1;
    gSave.kinstones.types[0] = 0x10;
    gSave.kinstones.types[18] = 0x11;
    gSave.kinstones.amounts[0] = 0x12;
    gSave.kinstones.amounts[18] = 0x13;
    gSave.kinstones.fuserProgress[0] = 0x5a;
    gSave.kinstones.fuserProgress[127] = 0x14;
    gSave.kinstones.fuserOffers[0] = 0x15;
    gSave.kinstones.fuserOffers[127] = 0x16;
    gSave.kinstones.fusedKinstones[0] = 0x17;
    gSave.kinstones.fusedKinstones[12] = 0x18;
    gSave.kinstones.fusionUnmarked[0] = 0x19;
    gSave.kinstones.fusionUnmarked[12] = 0x1a;
    gSave.areaVisitFlags[0] = 0x1b1c1d1e;
    gSave.areaVisitFlags[7] = 0x1f202122;
    gSave.name[0] = 0x23;
    gSave.name[5] = 0x24;
    gSave.figurines[0] = 0x2e;
    gSave.figurines[35] = 0x2f;
    gSave.inventory[0] = 0x25;
    gSave.inventory[33] = 0x26;
    gSave.flags[0] = 0x27;
    gSave.flags[0x1ff] = 0xa5;
    gSave.dungeonKeys[0] = 0x28;
    gSave.dungeonKeys[15] = 0x29;
    gSave.dungeonItems[0] = 0x2a;
    gSave.dungeonItems[15] = 0x2b;
    gSave.dungeonWarps[0] = 0x2c;
    gSave.dungeonWarps[15] = 0x2d;
    gSave.demo_timer = 0x10293847;
    gSoundPlayingInfo.currentBgm = 0x9abc;
    gActiveItems[0].behaviorId = 0x31;
    gActiveItems[0].animIndex = 0x4567;
    gActiveItems[0].field_0x18 = &gPlayerEntity.base;
    gActiveItems[1].field_0x18 = (Entity*)(uintptr_t)1;
    gPlayerEntity.base.prev = (Entity*)&gEntities[2];
    gPlayerEntity.base.next = (Entity*)(uintptr_t)1;
    gPlayerEntity.base.kind = PLAYER;
    gPlayerEntity.base.health = 0x55;
    gPlayerEntity.base.spriteVramOffset = 0x4567;
    gPlayerEntity.pulledJarEntity = &gAuxPlayerEntities[1].base;
    gPlayerEntity.carriedEntity = &gEntities[3].base;
    gPlayerEntity.unk_7a = 0x89ab;
    gPlayerEntity.unk_84.WORD_U = 0x10203040;
    gAuxPlayerEntities[0].base.kind = OBJECT;
    gAuxPlayerEntities[0].base.prev = &gPlayerEntity.base;
    gAuxPlayerEntities[0].field_0x68.HWORD = 0x1234;
    gEntities[0].base.kind = ENEMY;
    gEntities[0].base.parent = &gPlayerEntity.base;
    gEntities[0].field_0x7c.WORD_U = 0x55667788;
    gEntityLists[0].last = &gPlayerEntity.base;
    gEntityLists[0].first = &gEntities[0].base;
    gPlayerState.prevAnim = 0x41;
    gPlayerState.flags = 0x12345678;
    gPlayerState.path_memory[15] = 0x89abcdef;
    gPlayerState.item = &gEntities[3].base;
    gPlayerState.lilypad = (Entity*)(uintptr_t)1;
    gPlayerState.playerInput.playerMacro = (PlayerMacroEntry*)&gEwram[12];
    gPlayerState.controlMode = 0x63;
    gPlayerState.skills = 0xabcd;
    gMessage.state = 0x7f;
    gMessage.textIndex = 0x1234;
    gMessage.flags = 0x55667788;
    gMessage.rupees = 0x10203040;
    _gMenuSharedStorage[0] = 0xa2;
    gMenu.field_0xc = &gEwram[0x44];
    gGenericMenu.unk10.a[0] = 0xb3;
    gGenericMenu.unk2e.HALF.HI = 0xc4;
    gHUD.health = 0x45;
    gHUD.rupees = 0x6789;
    gHUD.elements[0].used = 1;
    gHUD.elements[0].x = 0x1234;
    gHUD.elements[0].framePtr = (Frame*)&rom[1];
    gHUD.elements[0].firstTile = (u32*)&gEwram[0x48];
    gHUD.elements[1].framePtr = (Frame*)(uintptr_t)1;
    gUI.nextToLoad = 0xd1;
    gUI.fadeType = 0x2345;
    gUI.currentRoomProperties = (void**)&gEwram[0x4c];
    gUI.mapBottomBgSettings = (BgSettings*)(uintptr_t)1;
    gUI.roomControls.reload_flags = 0x5678;
    gUI.roomControls.camera_target = &gPlayerEntity.base;
    gUI.roomControls.tileSet = 0x10203040;
    gUI.gfxSlotList.slots[0].status = 0xa;
    gUI.gfxSlotList.slots[0].paletteIndex = 0x89ab;
    gUI.gfxSlotList.slots[0].palettePointer = &gEwram[0x50];
    gUI.gfxSlotList.slots[1].palettePointer = (void*)(uintptr_t)1;
    gUI.palettes[0].objPaletteId = 0xcdef;
    gUI.unk_2a8[0] = 0xe1;
    gUI.unk_2a8[0xff] = 0xf2;
    gMapTop.bgSettings = (BgSettings*)(uintptr_t)1;
    gMapTop.mapData[0] = 0x1234;
    gMapTop.mapData[4095] = 0x5678;
    gMapTop.collisionData[0] = 0x9a;
    gMapTop.mapDataOriginal[0] = 0xbcde;
    gMapTop.tileTypes[0] = 0xfedc;
    gMapTop.tileIndices[0] = 0x1357;
    gMapTop.subTiles[0] = 0x2468;
    gMapTop.actTiles[4095] = 0xaa;
    gMapBottom.bgSettings = (BgSettings*)&gEwram[0x60];
    gMapBottom.mapData[0] = 0x4321;
    gArea.areaMetadata = 0x51;
    gArea.localFlagOffset = 0x1234;
    gArea.lightLevel = 0x5678;
    gArea.lightType = 0x9a;
    gArea.unk_0c_0 = 1;
    gArea.portal_x = 0xbcde;
    gArea.portal_timer = 0xef;
    gArea.unk28.textBaseIndex = 0xa1;
    gArea.unk28.ezloHintTexts[7] = 0x2468;
    gArea.roomResInfos[0].pixel_width = 0x1357;
    gArea.roomResInfos[0].tileSet = (MapDataDefinition*)&rom[1];
    gArea.currentRoomInfo.pixel_height = 0x89ab;
    gArea.currentRoomInfo.map = (MapDataDefinition*)(uintptr_t)1;
    gArea.pCurrentRoomInfo = &gArea.currentRoomInfo;
    gArea.bgm = 0x10203040;
    gRoomVars.didEnterScrolling = 1;
    gRoomVars.needHealthDrop = 0xa2;
    gRoomVars.lightLevel = -2;
    gRoomVars.tileEntityCount = 0x4567;
    gRoomVars.graphicsGroups[3] = 0x89;
    gRoomVars.flags[51] = 0xab;
    gRoomVars.currentAreaDroptable.a[15] = 0x1234;
    gRoomVars.animFlags = 0x55667788;
    gRoomVars.properties[0] = &rom[2];
    gRoomVars.properties[1] = (void*)(uintptr_t)1;
    gRoomVars.entityRails[0] = &gEwram[0x64];
    gRoomVars.puzzleEntities[0] = &gEntities[2].base;
    gRoomVars.puzzleEntities[1] = (Entity*)(uintptr_t)1;
    gOAMControls.field_0x0 = 0x31;
    gOAMControls.field_0x1 = 0x32;
    gOAMControls.spritesOffset = 0x33;
    gOAMControls.updated = 0x34;
    gOAMControls._4 = 0x4567;
    gOAMControls._6 = 0x89ab;
    gOAMControls._0[0] = 0x35;
    gOAMControls._0[23] = 0x36;
    gOAMControls.oam[0].y = 0x7f;
    gOAMControls.oam[0].affineMode = 3;
    gOAMControls.oam[0].objMode = 2;
    gOAMControls.oam[0].mosaic = 1;
    gOAMControls.oam[0].bpp = 1;
    gOAMControls.oam[0].shape = 1;
    gOAMControls.oam[0].x = 0x1ab;
    gOAMControls.oam[0].matrixNum = 0x12;
    gOAMControls.oam[0].size = 2;
    gOAMControls.oam[0].tileNum = 0x2cd;
    gOAMControls.oam[0].priority = 3;
    gOAMControls.oam[0].paletteNum = 0xa;
    gOAMControls.oam[0].affineParam = 0x1357;
    gOAMControls.oam[127].tileNum = 0x123;
    gScreen.lcd.displayControl = 0x4567;
    gScreen.lcd.unk4 = 0x89ab;
    gScreen.lcd.displayControlMask = 0xcdef;
    gScreen.bg0.control = 0x1122;
    gScreen.bg0.xOffset = 0x3344;
    gScreen.bg0.yOffset = 0x5566;
    gScreen.bg0.updated = 0x7788;
    gScreen.bg0.subTileMap = &gEwram[0x70];
    gScreen.bg1.subTileMap = (void*)(uintptr_t)1;
    gScreen.bg2.xOffset = -2;
    gScreen.bg2.subTileMap = &rom[3];
    gScreen.bg3.yOffset = -3;
    gScreen.bg3.subTileMap = &gIwram[0x74];
    gScreen.controls.alphaBlend = 0x2468;
    gScreen.vBlankDMA.ready = 1;
    gScreen.vBlankDMA.readyBackup = 2;
    gScreen.vBlankDMA.unused = 0x9abc;
    gScreen.vBlankDMA.src = (u16*)&gEwram[0x78];
    gScreen.vBlankDMA.dest = (u16*)(uintptr_t)1;
    gScreen.vBlankDMA.size = 0x10203040;
    gBgAnimations[0].currentFrame = (const BgAnimationFrame*)&rom[1];
    gBgAnimations[0].unk_4 = 0x4567;
    gBgAnimations[0].timer = 0x89ab;
    gBgAnimations[1].currentFrame = (const BgAnimationFrame*)(uintptr_t)1;
    gBgAnimations[7].timer = 0xcdef;
    gPauseMenuOptions.disabled = 0x41;
    gPauseMenuOptions.screen = 0x42;
    gPauseMenuOptions.unk2[0] = 0x43;
    gPauseMenuOptions.unk2[14] = 0x44;
    gPauseMenuOptions.unk11 = 0x45;
    gPauseMenuOptions.unk12 = 0x46;
    gPauseMenuOptions.unk13 = 0x47;
    gPauseMenuOptions.screen2 = -2;
    gPauseMenuOptions.unk15 = 0x48;
    gPauseMenuOptions.unk16 = -3;
    gPauseMenuOptions.unk17 = 0x49;
    gMapDataTopSpecial[0] = 0x1234;
    gMapDataTopSpecial[0x1fff] = 0xabcd;
    gMapDataTopSpecial[0x2000] = 0xbeef;
    gUnk_02006F00[0] = 0xa5;
    gEwram[0x6f00] = 0x5a;
    gScriptExecutionContextArray[0].scriptInstructionPointer = (Script*)&rom[1];
    gScriptExecutionContextArray[0].intVariable = 0x10203040;
    gScriptExecutionContextArray[0].postScriptActions = 0x50607080;
    gScriptExecutionContextArray[0].unk_0C[0] = 0x91;
    gScriptExecutionContextArray[0].unk_0C[3] = 0x94;
    gScriptExecutionContextArray[0].wait = 0x1234;
    gScriptExecutionContextArray[0].unk_12 = 0x5678;
    gScriptExecutionContextArray[0].condition = 0x90abcdef;
    gScriptExecutionContextArray[0].unk_18 = 0xa1;
    gScriptExecutionContextArray[0].unk_19 = 0xa2;
    gScriptExecutionContextArray[0].unk_1A = 0xa3;
    gScriptExecutionContextArray[0].unk_1B = 0xa4;
    gScriptExecutionContextArray[0].x.WORD_U = 0x11223344;
    gScriptExecutionContextArray[0].y.WORD_U = 0x55667788;
    gScriptExecutionContextArray[1].scriptInstructionPointer = (Script*)&gEwram[0x6a];
    gScriptExecutionContextArray[1].intVariable = 0x89abcdef;
    gScriptExecutionContextArray[31].scriptInstructionPointer = (Script*)(uintptr_t)1;
    memset(gUnk_02034492, 0x91, sizeof(gUnk_02034492));
    memset(&gEwram[0x34492], 0x72, 0x16);
    gRomData = rom;
    gRomSize = sizeof(rom);
    TmcRaMemory_Publish();
    CHECK(strlen(TmcRaMemory_ManifestHash()) == 64);
    CHECK(TmcRaMemory_Current().save_validated);
    CHECK(TmcRaMemory_Current().bytes[TMC_RA_SAVE_OFFSET] == 0);
    CHECK(TmcRaMemory_Current().bytes[TMC_RA_SAVE_OFFSET + 7] == 7);
    CHECK(TmcRaMemory_Current().bytes[0] == 0x31);
    CHECK(TmcRaMemory_Current().bytes[0x0000] == 0x31 && TmcRaMemory_Current().bytes[0x0007] == 0x89);
    CHECK(TmcRaMemory_Current().bytes[0x0008] == 0x35 && TmcRaMemory_Current().bytes[0x001f] == 0x36);
    CHECK(TmcRaMemory_Current().bytes[0x0020] == 0x7f && TmcRaMemory_Current().bytes[0x0021] == 0x7b);
    CHECK(TmcRaMemory_Current().bytes[0x0022] == 0xab && TmcRaMemory_Current().bytes[0x0023] == 0xa5);
    CHECK(TmcRaMemory_Current().bytes[0x0024] == 0xcd && TmcRaMemory_Current().bytes[0x0025] == 0xae);
    CHECK(TmcRaMemory_Current().bytes[0x0026] == 0x57 && TmcRaMemory_Current().bytes[0x0027] == 0x13);
    CHECK(TmcRaMemory_Current().bytes[0x041c] == 0x23 && TmcRaMemory_Current().validated[0x041f]);
    CHECK(TmcRaMemory_Current().bytes[0x0f50] == 0x67 && TmcRaMemory_Current().bytes[0x0f57] == 0xcd);
    CHECK(TmcRaMemory_Current().bytes[0x0f58] == 0x22 && TmcRaMemory_Current().bytes[0x0f5f] == 0x77);
    CHECK(TmcRaMemory_Current().bytes[0x0f60] == 0x70 && TmcRaMemory_Current().bytes[0x0f63] == 0x02);
    CHECK(!TmcRaMemory_Current().validated[0x0f6c] && !TmcRaMemory_Current().validated[0x0f6f]);
    CHECK(TmcRaMemory_Current().bytes[0x0f72] == 0xfe && TmcRaMemory_Current().bytes[0x0f73] == 0xff);
    CHECK(TmcRaMemory_Current().bytes[0x0f78] == 0x03 && TmcRaMemory_Current().bytes[0x0f7b] == 0x08);
    CHECK(TmcRaMemory_Current().bytes[0x0fb8] == 0x68 && TmcRaMemory_Current().bytes[0x0fb9] == 0x24);
    CHECK(TmcRaMemory_Current().bytes[0x0fbc] == 1 && TmcRaMemory_Current().bytes[0x0fbf] == 0x9a);
    CHECK(TmcRaMemory_Current().bytes[0x0fc0] == 0x78 && TmcRaMemory_Current().bytes[0x0fc3] == 0x02);
    CHECK(!TmcRaMemory_Current().validated[0x0fc4] && !TmcRaMemory_Current().validated[0x0fc7]);
    CHECK(TmcRaMemory_Current().bytes[0x08cc0] == 0x01 && TmcRaMemory_Current().bytes[0x08cc3] == 0x08);
    CHECK(TmcRaMemory_Current().bytes[0x08cc4] == 0x67 && TmcRaMemory_Current().bytes[0x08cc7] == 0x89);
    CHECK(!TmcRaMemory_Current().validated[0x08cc8] && !TmcRaMemory_Current().validated[0x08ccb]);
    CHECK(TmcRaMemory_Current().bytes[0x08cfe] == 0xef && TmcRaMemory_Current().bytes[0x08cff] == 0xcd);
    CHECK(TmcRaMemory_Current().bytes[0x3c490] == 0x41 && TmcRaMemory_Current().bytes[0x3c491] == 0x42);
    for (unsigned i = 0x02; i < 0x18; ++i)
        CHECK(!TmcRaMemory_Current().validated[0x3c490 + i]);
    CHECK(TmcRaMemory_Current().bytes[0x8000] == 0x22);
    CHECK(TmcRaMemory_Current().bytes[0x1000] == 0xa1 && TmcRaMemory_Current().bytes[0x100c] == 0x34);
    CHECK(TmcRaMemory_Current().bytes[0x100d] == 0x12);
    CHECK(!TmcRaMemory_Current().validated[0x1000] && TmcRaMemory_Current().validated[0x100b] &&
          !TmcRaMemory_Current().validated[0x100c] &&
          TmcRaMemory_Current().validated[0x100d]);
    CHECK(TmcRaMemory_Current().bytes[0x0ff0] == 0x78 && TmcRaMemory_Current().bytes[0x0ff7] == 0x9a);
    CHECK(TmcRaMemory_Current().validated[0x0ff6] && !TmcRaMemory_Current().validated[0x0ff7] &&
          !TmcRaMemory_Current().validated[0x0ff8]);
    CHECK(TmcRaMemory_Current().bytes[0x0fd0] == 0xa2 && TmcRaMemory_Current().bytes[0x0fd4] == 0xef);
    CHECK(TmcRaMemory_Current().validated[0x0fdb] && !TmcRaMemory_Current().validated[0x0fdc] &&
          TmcRaMemory_Current().validated[0x0fdd]);
    CHECK(TmcRaMemory_Current().bytes[0x0fd7] == 0x89 && TmcRaMemory_Current().bytes[0x0fe9] == 0x13);
    CHECK(TmcRaMemory_Current().bytes[0x0bf0] == 0xb4 && TmcRaMemory_Current().bytes[0x0bf6] == 0x34);
    CHECK(TmcRaMemory_Current().bytes[0x0bfc] == 0xfe && TmcRaMemory_Current().bytes[0x0c1c] == 0x44);
    CHECK(TmcRaMemory_Current().bytes[0x0c1f] == 0x11);
    CHECK(TmcRaMemory_Current().bytes[0x0c20] == 0x28 && TmcRaMemory_Current().bytes[0x0c23] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x0c24] == 0x11 && !TmcRaMemory_Current().validated[0x0c24]);
    CHECK(TmcRaMemory_Current().bytes[0x10a0] == 0x44 && TmcRaMemory_Current().bytes[0x10a3] == 0x11);
    CHECK(TmcRaMemory_Current().bytes[0x114e] == 0xef && TmcRaMemory_Current().bytes[0x114f] == 0xbe);
    CHECK(TmcRaMemory_Current().bytes[0x1150] == 0x78 && TmcRaMemory_Current().bytes[0x1153] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x0b80] == 0 && TmcRaMemory_Current().bytes[0x0b81] == 0x31);
    CHECK(TmcRaMemory_Current().bytes[0x0b90] == 0x67 && TmcRaMemory_Current().bytes[0x0b91] == 0x45);
    CHECK(TmcRaMemory_Current().bytes[0x0b98] == 0x60 && TmcRaMemory_Current().bytes[0x0b9b] == 0x03);
    CHECK(!TmcRaMemory_Current().validated[0x0bb4] && !TmcRaMemory_Current().validated[0x0bb7]);
    CHECK(TmcRaMemory_Current().bytes[0x1160] == 0xb0 && TmcRaMemory_Current().bytes[0x1163] == 0x03);
    CHECK(!TmcRaMemory_Current().validated[0x1164] && !TmcRaMemory_Current().validated[0x1167]);
    CHECK(TmcRaMemory_Current().bytes[0x1168] == PLAYER && TmcRaMemory_Current().bytes[0x11a5] == 0x55);
    CHECK(TmcRaMemory_Current().bytes[0x11c0] == 0x67 && TmcRaMemory_Current().bytes[0x11c1] == 0x45);
    CHECK(TmcRaMemory_Current().bytes[0x11d0] == 0x70 && TmcRaMemory_Current().bytes[0x11d3] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x11d4] == 0x38 && TmcRaMemory_Current().bytes[0x11d7] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x11da] == 0xab && TmcRaMemory_Current().bytes[0x11e4] == 0x40);
    CHECK(TmcRaMemory_Current().bytes[0x11e8] == 0x60 && TmcRaMemory_Current().bytes[0x11eb] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x11f0] == OBJECT && TmcRaMemory_Current().bytes[0x1250] == 0x34);
    CHECK(!TmcRaMemory_Current().validated[0x126c] && !TmcRaMemory_Current().validated[0x126f]);
    CHECK(TmcRaMemory_Current().bytes[0x15a8] == ENEMY && TmcRaMemory_Current().bytes[0x15f0] == 0x60);
    CHECK(TmcRaMemory_Current().bytes[0x161c] == 0x88 && TmcRaMemory_Current().bytes[0x161f] == 0x55);
    CHECK(!TmcRaMemory_Current().validated[0x1624] && !TmcRaMemory_Current().validated[0x1627]);
    CHECK(TmcRaMemory_Current().bytes[0x3d70] == 0x60 && TmcRaMemory_Current().bytes[0x3d73] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x3d74] == 0xa0 && TmcRaMemory_Current().bytes[0x3d77] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x3f80] == 0x41 && TmcRaMemory_Current().bytes[0x3fb0] == 0x78);
    CHECK(TmcRaMemory_Current().bytes[0x3fac] == 0x38 && TmcRaMemory_Current().bytes[0x3faf] == 0x03);
    CHECK(TmcRaMemory_Current().bytes[0x3ffc] == 0xef && TmcRaMemory_Current().bytes[0x3fff] == 0x89);
    CHECK(!TmcRaMemory_Current().validated[0x4004] && !TmcRaMemory_Current().validated[0x4007]);
    CHECK(TmcRaMemory_Current().bytes[0x400b] == 0x63);
    CHECK(TmcRaMemory_Current().bytes[0x401c] == 0x0c && TmcRaMemory_Current().bytes[0x401f] == 0x02);
    CHECK(TmcRaMemory_Current().bytes[0x402c] == 0xcd);
    CHECK(TmcRaMemory_Current().bytes[0x2c050] == 0x44 && TmcRaMemory_Current().bytes[0x2c088] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x2c08f] == 0x55);
    CHECK(TmcRaMemory_Current().bytes[0x13654] == 0x34 && TmcRaMemory_Current().bytes[0x13655] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x15652] == 0x78 && TmcRaMemory_Current().bytes[0x15653] == 0x56);
    CHECK(TmcRaMemory_Current().bytes[0x15654] == 0x9a && TmcRaMemory_Current().bytes[0x16654] == 0xde);
    CHECK(TmcRaMemory_Current().bytes[0x18654] == 0xdc && TmcRaMemory_Current().bytes[0x19654] == 0x57);
    CHECK(TmcRaMemory_Current().bytes[0x1a654] == 0x68 && TmcRaMemory_Current().bytes[0x1f653] == 0xaa);
    CHECK(!TmcRaMemory_Current().validated[0x13650] && !TmcRaMemory_Current().validated[0x13653]);
    CHECK(TmcRaMemory_Current().bytes[0x2deb0] == 0x60 && TmcRaMemory_Current().bytes[0x2deb3] == 0x02);
    CHECK(TmcRaMemory_Current().bytes[0x2deb4] == 0x21 && TmcRaMemory_Current().bytes[0x2deb5] == 0x43);
    CHECK(TmcRaMemory_Current().bytes[0x3ba90] == 0x51 && TmcRaMemory_Current().bytes[0x3ba94] == 0x34);
    CHECK(TmcRaMemory_Current().bytes[0x3ba9a] == 0x78 && TmcRaMemory_Current().bytes[0x3ba9c] == 0x9a);
    CHECK(TmcRaMemory_Current().bytes[0x3ba9d] == 0x01);
    CHECK(TmcRaMemory_Current().bytes[0x3baa2] == 0xde && TmcRaMemory_Current().bytes[0x3baaa] == 0xef);
    CHECK(TmcRaMemory_Current().bytes[0x3bab8] == 0xa1 && TmcRaMemory_Current().bytes[0x3baca] == 0x68);
    CHECK(TmcRaMemory_Current().bytes[0x3bad0] == 0x57 && TmcRaMemory_Current().bytes[0x3bad1] == 0x13);
    CHECK(TmcRaMemory_Current().bytes[0x3bad8] == 0x01 && TmcRaMemory_Current().bytes[0x3badb] == 0x08);
    CHECK(TmcRaMemory_Current().bytes[0x3c2d2] == 0xab && TmcRaMemory_Current().bytes[0x3c2d3] == 0x89);
    CHECK(!TmcRaMemory_Current().validated[0x3c2dc] && !TmcRaMemory_Current().validated[0x3c2df]);
    CHECK(TmcRaMemory_Current().bytes[0x3c2f0] == 0xd0 && TmcRaMemory_Current().bytes[0x3c2f3] == 0x02);
    CHECK(TmcRaMemory_Current().bytes[0x3c2f4] == 0x40 && TmcRaMemory_Current().bytes[0x3c2f7] == 0x10);
    CHECK(!TmcRaMemory_Current().validated[0x3c2f8]);
    CHECK(!TmcRaMemory_Current().validated[0x3ba97] && !TmcRaMemory_Current().validated[0x3ba9e] &&
          !TmcRaMemory_Current().validated[0x3baab]);
    CHECK(TmcRaMemory_Current().bytes[0x3c350] == 1 && TmcRaMemory_Current().bytes[0x3c35a] == 0xa2);
    CHECK(TmcRaMemory_Current().bytes[0x3c35c] == 0xfe && TmcRaMemory_Current().bytes[0x3c35f] == 0x45);
    CHECK(TmcRaMemory_Current().bytes[0x3c363] == 0x89 && TmcRaMemory_Current().bytes[0x3c397] == 0xab);
    CHECK(TmcRaMemory_Current().bytes[0x3c3b6] == 0x34 && TmcRaMemory_Current().bytes[0x3c3b7] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x3c3bc] == 0x02 && TmcRaMemory_Current().bytes[0x3c3bf] == 0x08);
    CHECK(!TmcRaMemory_Current().validated[0x3c3c0] && !TmcRaMemory_Current().validated[0x3c3c3]);
    CHECK(TmcRaMemory_Current().bytes[0x3c3dc] == 0x64 && TmcRaMemory_Current().bytes[0x3c3df] == 0x02);
    CHECK(TmcRaMemory_Current().bytes[0x3c3fc] == 0xb0 && TmcRaMemory_Current().bytes[0x3c3ff] == 0x03);
    CHECK(!TmcRaMemory_Current().validated[0x3c400] && !TmcRaMemory_Current().validated[0x3c403]);
    CHECK(!TmcRaMemory_Current().validated[0x3c35b]);
    first_view = TmcRaMemory_Current();
    gMapTop.mapData[0] = 0xbeef;
    gArea.unk_0c_1 = 5;
    gArea.unk_0c_4 = 0xa;
    gOAMControls.oam[0].y = 0x55;
    gScreen.bg0.subTileMap = (void*)(uintptr_t)1;
    gBgAnimations[0].currentFrame = (const BgAnimationFrame*)(uintptr_t)1;
    gPauseMenuOptions.screen = 0x5a;
    TmcRaMemory_Publish();
    CHECK(first_view.bytes[0x0020] == 0x7f && first_view.bytes[0x0f60] == 0x70 &&
          first_view.bytes[0x08cc0] == 0x01 && first_view.bytes[0x3c491] == 0x42);
    CHECK(first_view.bytes[0x13654] == 0x34 && first_view.bytes[0x13655] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x0020] == 0x55);
    CHECK(!TmcRaMemory_Current().validated[0x0f60] && !TmcRaMemory_Current().validated[0x0f63]);
    CHECK(!TmcRaMemory_Current().validated[0x08cc0] && !TmcRaMemory_Current().validated[0x08cc3]);
    CHECK(TmcRaMemory_Current().bytes[0x3c491] == 0x5a && TmcRaMemory_Current().validated[0x3c491]);
    memset(gPauseMenuOptions.unk2, 0xa2, sizeof(gPauseMenuOptions.unk2));
    memset(gUnk_02034492, 0xb3, sizeof(gUnk_02034492));
    memset(&gEwram[0x34492], 0xc4, 0x16);
    TmcRaMemory_Publish();
    for (unsigned i = 0x02; i < 0x18; ++i)
        CHECK(!TmcRaMemory_Current().validated[0x3c490 + i]);
    CHECK(gPauseMenuOptions.unk2[0] == 0xa2 && gUnk_02034492[0] == 0xb3 &&
          TmcRaMemory_Current().bytes[0x3c492] == 0xc4);
    memset(gPauseMenuOptions.unk2, 0xd5, sizeof(gPauseMenuOptions.unk2));
    memset(gUnk_02034492, 0xe6, sizeof(gUnk_02034492));
    memset(&gEwram[0x34492], 0xf7, 0x16);
    TmcRaMemory_Publish();
    for (unsigned i = 0x02; i < 0x18; ++i)
        CHECK(!TmcRaMemory_Current().validated[0x3c490 + i]);
    CHECK(gPauseMenuOptions.unk2[0] == 0xd5 && gUnk_02034492[0] == 0xe6 &&
          TmcRaMemory_Current().bytes[0x3c492] == 0xf7);
    CHECK(TmcRaMemory_Current().bytes[0x13654] == 0xef && TmcRaMemory_Current().bytes[0x13655] == 0xbe);
    CHECK(TmcRaMemory_Current().bytes[0x3ba9d] == 0xab);
    CHECK(TmcRaMemory_Current().bytes[0x0aa40] == 1 && TmcRaMemory_Current().bytes[0x0aa48] == 0x55);
    CHECK(TmcRaMemory_Current().bytes[0x0aa60] == 0x34 && TmcRaMemory_Current().bytes[0x0aa61] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x0aac8] == 2 && TmcRaMemory_Current().bytes[0x0aacc] == 0xfc);
    CHECK(TmcRaMemory_Current().bytes[0x0aae8] == 3 && TmcRaMemory_Current().bytes[0x0aafa] == 0x6b);
    CHECK(TmcRaMemory_Current().bytes[0x0ab00] == 0x67 && TmcRaMemory_Current().bytes[0x0ab01] == 0x45);
    CHECK(TmcRaMemory_Current().bytes[0x0aaa0] == 0x1e && TmcRaMemory_Current().bytes[0x0aabc] == 0x22);
    CHECK(TmcRaMemory_Current().bytes[0x0aac0] == 0x23 && TmcRaMemory_Current().bytes[0x0aac5] == 0x24);
    CHECK(!TmcRaMemory_Current().validated[0x0aad1]);
    CHECK(!TmcRaMemory_Current().validated[0x0aafc]);
    CHECK(!TmcRaMemory_Current().validated[0x0ab0c] && !TmcRaMemory_Current().validated[0x0ab0d]);
    CHECK(TmcRaMemory_Current().bytes[0x0ab0e] == 0x2e &&
          TmcRaMemory_Current().bytes[0x0ab31] == 0x2f);
    CHECK(TmcRaMemory_Current().validated[0x0ab0e] && TmcRaMemory_Current().validated[0x0ab31]);
    CHECK(TmcRaMemory_Current().bytes[0x0ab32] == 0x25 && TmcRaMemory_Current().bytes[0x0ab53] == 0x26);
    CHECK(TmcRaMemory_Current().bytes[0x0ab56] == 1 && TmcRaMemory_Current().bytes[0x0ab58] == 0x10);
    CHECK(TmcRaMemory_Current().bytes[0x0ab6a] == 0x11 && TmcRaMemory_Current().bytes[0x0ab6b] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x0ab7d] == 0x13 && TmcRaMemory_Current().bytes[0x0ab81] == 0x5a);
    CHECK(TmcRaMemory_Current().bytes[0x0ac00] == 0x14 && TmcRaMemory_Current().bytes[0x0ac01] == 0x15);
    CHECK(TmcRaMemory_Current().bytes[0x0ac80] == 0x16 && TmcRaMemory_Current().bytes[0x0ac81] == 0x17);
    CHECK(TmcRaMemory_Current().bytes[0x0ac8d] == 0x18 && TmcRaMemory_Current().bytes[0x0ac8e] == 0x19);
    CHECK(TmcRaMemory_Current().bytes[0x0ac9a] == 0x1a);
    CHECK(!TmcRaMemory_Current().validated[0x0ac9b]);
    CHECK(TmcRaMemory_Current().bytes[0x29ef4] == 0xbc && TmcRaMemory_Current().bytes[0x29ef5] == 0x9a);
    CHECK(TmcRaMemory_Current().validated[0x29ef4] && TmcRaMemory_Current().validated[0x29ef5]);
    CHECK(TmcRaMemory_Current().bytes[0x0ac9c] == 0x27 && TmcRaMemory_Current().bytes[0x0ae9b] == 0xa5);
    CHECK(TmcRaMemory_Current().bytes[0x0ae9c] == 0x28 && TmcRaMemory_Current().bytes[0x0aeab] == 0x29);
    CHECK(TmcRaMemory_Current().bytes[0x0aeac] == 0x2a && TmcRaMemory_Current().bytes[0x0aebb] == 0x2b);
    CHECK(TmcRaMemory_Current().bytes[0x0aebc] == 0x2c && TmcRaMemory_Current().bytes[0x0aecb] == 0x2d);
    CHECK(TmcRaMemory_Current().bytes[0x0aee8] == 0x47);
    CHECK(!TmcRaMemory_Current().validated[0x0aa44]);
    CHECK(!TmcRaMemory_Current().validated[0x0ab54]);
    CHECK(!TmcRaMemory_Current().validated[0x0aeec]);
    CHECK(TmcRaMemory_Current().bytes[0x0af00] == 0x34 && TmcRaMemory_Current().bytes[0x0af01] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x0eeff] == 0xab && TmcRaMemory_Current().bytes[0x0eefe] == 0xcd);
    CHECK(TmcRaMemory_Current().validated[0x0af00] && TmcRaMemory_Current().validated[0x0eeff]);
    CHECK(TmcRaMemory_Current().bytes[0x0af00] != gEwram[0x02f00]);
    CHECK(TmcRaMemory_Current().bytes[0x0ef00] == 0x5a);
    CHECK(!TmcRaMemory_Current().validated[0x0ef00]);
    CHECK(gMapDataTopSpecial[0x2000] == 0xbeef && gUnk_02006F00[0] == 0xa5);
    CHECK(TmcRaMemory_OverlayExplicit(0x0ef00, explicit_bytes, 1));
    TmcRaMemory_Publish();
    CHECK(!TmcRaMemory_Current().validated[0x0ef00]);
    CHECK(TmcRaMemory_OverlayExplicit(0x0ef00, explicit_bytes, 1));
    TmcRaMemory_Publish();
    CHECK(!TmcRaMemory_Current().validated[0x0ef00]);
    TmcRaMemory_Publish();
    CHECK(!TmcRaMemory_Current().validated[0x0ef00]);
    CHECK(TmcRaMemory_Current().bytes[0x08050] == 0x7f && TmcRaMemory_Current().bytes[0x08058] == 0x34);
    CHECK(TmcRaMemory_Current().bytes[0x08059] == 0x12 && TmcRaMemory_Current().bytes[0x0805c] == 0x88);
    CHECK(TmcRaMemory_Current().bytes[0x08060] == 0x40 && TmcRaMemory_Current().validated[0x0806f]);
    CHECK(TmcRaMemory_Current().bytes[0x08080] == 0xa2 && TmcRaMemory_Current().bytes[0x0808c] == 0x44);
    CHECK(TmcRaMemory_Current().bytes[0x0808f] == 0x02 && TmcRaMemory_Current().bytes[0x08090] == 0xb3);
    CHECK(TmcRaMemory_Current().bytes[0x080af] == 0xc4 && TmcRaMemory_Current().validated[0x080af]);
    CHECK(TmcRaMemory_Current().bytes[0x12f03] == 0x45 && TmcRaMemory_Current().bytes[0x12f0e] == 0x89);
    CHECK(TmcRaMemory_Current().bytes[0x12f0f] == 0x67 && TmcRaMemory_Current().bytes[0x12f34] == 1);
    CHECK(TmcRaMemory_Current().bytes[0x12f40] == 0x34 && TmcRaMemory_Current().bytes[0x12f41] == 0x12);
    CHECK(TmcRaMemory_Current().bytes[0x12f48] == 1 && TmcRaMemory_Current().bytes[0x12f4b] == 0x08);
    CHECK(TmcRaMemory_Current().bytes[0x12f50] == 0x48 && TmcRaMemory_Current().bytes[0x12f53] == 0x02);
    CHECK(!TmcRaMemory_Current().validated[0x12f68] && !TmcRaMemory_Current().validated[0x12f6b]);
    CHECK(TmcRaMemory_Current().bytes[0x3aec0] == 0xd1 && TmcRaMemory_Current().bytes[0x3aec8] == 0x45);
    CHECK(TmcRaMemory_Current().bytes[0x3aed0] == 0x4c && TmcRaMemory_Current().bytes[0x3aed3] == 0x02);
    CHECK(!TmcRaMemory_Current().validated[0x3aed4] && !TmcRaMemory_Current().validated[0x3aed7]);
    CHECK(TmcRaMemory_Current().bytes[0x3aedc] == 0x78 && TmcRaMemory_Current().bytes[0x3af0c] == 0x60);
    CHECK(TmcRaMemory_Current().bytes[0x3af0f] == 0x03 && !TmcRaMemory_Current().validated[0x3af10]);
    CHECK(TmcRaMemory_Current().bytes[0x3af14] == 0x0a && TmcRaMemory_Current().bytes[0x3af1a] == 0xab);
    CHECK(TmcRaMemory_Current().bytes[0x3af1b] == 0x89 && TmcRaMemory_Current().bytes[0x3af1c] == 0x50);
    CHECK(TmcRaMemory_Current().bytes[0x3af1f] == 0x02 && !TmcRaMemory_Current().validated[0x3af28]);
    CHECK(TmcRaMemory_Current().bytes[0x3b12a] == 0xef && TmcRaMemory_Current().bytes[0x3b12b] == 0xcd);
    CHECK(TmcRaMemory_Current().bytes[0x3b168] == 0xe1 && TmcRaMemory_Current().bytes[0x3b267] == 0xf2);
    CHECK(!TmcRaMemory_Current().validated[0x3b268] && !TmcRaMemory_Current().validated[0x3b273]);
    CHECK(TmcRaMemory_Current().bytes[0x3e570] == 0x01 && TmcRaMemory_Current().bytes[0x3e573] == 0x08);
    CHECK(TmcRaMemory_Current().bytes[0x3e574] == 0x40 && TmcRaMemory_Current().bytes[0x3e577] == 0x10);
    CHECK(TmcRaMemory_Current().bytes[0x3e578] == 0x80 && TmcRaMemory_Current().bytes[0x3e57b] == 0x50);
    CHECK(TmcRaMemory_Current().bytes[0x3e57c] == 0x91 && TmcRaMemory_Current().bytes[0x3e57f] == 0x94);
    CHECK(TmcRaMemory_Current().bytes[0x3e580] == 0x34 && TmcRaMemory_Current().bytes[0x3e583] == 0x56);
    CHECK(TmcRaMemory_Current().bytes[0x3e584] == 0xef && TmcRaMemory_Current().bytes[0x3e587] == 0x90);
    CHECK(TmcRaMemory_Current().bytes[0x3e588] == 0xa1 && TmcRaMemory_Current().bytes[0x3e58b] == 0xa4);
    CHECK(TmcRaMemory_Current().bytes[0x3e58c] == 0x44 && TmcRaMemory_Current().bytes[0x3e58f] == 0x11);
    CHECK(TmcRaMemory_Current().bytes[0x3e590] == 0x88 && TmcRaMemory_Current().bytes[0x3e593] == 0x55);
    CHECK(TmcRaMemory_Current().bytes[0x3e594] == 0x6a && TmcRaMemory_Current().bytes[0x3e597] == 0x02);
    CHECK(TmcRaMemory_Current().bytes[0x3e598] == 0xef && TmcRaMemory_Current().bytes[0x3e59b] == 0x89);
    CHECK(!TmcRaMemory_Current().validated[0x3e9cc] && !TmcRaMemory_Current().validated[0x3e9cf]);
    gMain.task = TASK_TITLE;
    _gMenuSharedStorage[0x0c] = 0xd2;
    _gMenuSharedStorage[0x2f] = 0xd3;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0xd2 && TmcRaMemory_Current().validated[0x0808c]);
    CHECK(TmcRaMemory_Current().bytes[0x080af] == 0xd3 && TmcRaMemory_Current().validated[0x080af]);
    gMain.task = TASK_FILE_SELECT;
    gUI.lastState = 0; /* FileSelectState STATE_NONE */
    _gMenuSharedStorage[0x0c] = 0xd4;
    _gMenuSharedStorage[0x2f] = 0xd5;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0xd4 && TmcRaMemory_Current().validated[0x0808c]);
    CHECK(TmcRaMemory_Current().bytes[0x080af] == 0xd5 && TmcRaMemory_Current().validated[0x080af]);
    gUI.lastState = 1; /* FileSelectState STATE_NEW */
    gMenu.field_0xc = &gEwram[0x54];
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0x54 && TmcRaMemory_Current().bytes[0x0808f] == 0x02);
    CHECK(TmcRaMemory_Current().validated[0x0808c] && TmcRaMemory_Current().validated[0x0808d] &&
          TmcRaMemory_Current().validated[0x0808e] && TmcRaMemory_Current().validated[0x0808f]);
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0x54 && TmcRaMemory_Current().bytes[0x0808f] == 0x02);
    CHECK(TmcRaMemory_Current().validated[0x0808c] && TmcRaMemory_Current().validated[0x0808d] &&
          TmcRaMemory_Current().validated[0x0808e] && TmcRaMemory_Current().validated[0x0808f]);
    gMenu.field_0xc = (u8*)(uintptr_t)1;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0 && TmcRaMemory_Current().bytes[0x0808d] == 0 &&
          TmcRaMemory_Current().bytes[0x0808e] == 0 && TmcRaMemory_Current().bytes[0x0808f] == 0);
    CHECK(!TmcRaMemory_Current().validated[0x0808c] && !TmcRaMemory_Current().validated[0x0808d] &&
          !TmcRaMemory_Current().validated[0x0808e] && !TmcRaMemory_Current().validated[0x0808f]);
    gScriptExecutionContextArray[31].scriptInstructionPointer = (Script*)&rom[2];
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x3e9cc] == 0x02 && TmcRaMemory_Current().bytes[0x3e9cc + 3] == 0x08);
    CHECK(TmcRaMemory_Current().validated[0x3e9cc] && TmcRaMemory_Current().validated[0x3e9cf]);
    gScriptExecutionContextArray[31].scriptInstructionPointer = (Script*)(uintptr_t)1;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x3e9cc] == 0 && TmcRaMemory_Current().bytes[0x3e9cf] == 0);
    CHECK(!TmcRaMemory_Current().validated[0x3e9cc] && !TmcRaMemory_Current().validated[0x3e9cf]);
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0808c] == 0 && TmcRaMemory_Current().bytes[0x0808d] == 0 &&
          TmcRaMemory_Current().bytes[0x0808e] == 0 && TmcRaMemory_Current().bytes[0x0808f] == 0);
    CHECK(!TmcRaMemory_Current().validated[0x0808c] && !TmcRaMemory_Current().validated[0x0808d] &&
          !TmcRaMemory_Current().validated[0x0808e] && !TmcRaMemory_Current().validated[0x0808f]);
    gMain.task = TASK_GAMEOVER;
    CHECK(!TmcRaMemory_Current().validated[0x0420]);
    CHECK(!TmcRaMemory_Current().validated[0x0fea]);
    CHECK(!TmcRaMemory_Current().validated[0x0420]);
    CHECK(TmcRaMemory_Read(0x100c, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_Read(0x0c20, &valid) == 0x28 && valid);
    CHECK(TmcRaMemory_Read(0x0aa44, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_Read(0x0af00, &valid) == 0x34 && valid);
    CHECK(TmcRaMemory_Read(0x0420, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_Read(0x8000, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_Read(TMC_RA_SAVE_OFFSET, &valid) == 0 && valid);
    CHECK(TmcRaMemory_Read(TMC_RA_SAVE_OFFSET + 7, &valid) == 7 && valid);
    CHECK(TmcRaMemory_Read(TMC_RA_SAVE_OFFSET + PORT_SAVE_EEPROM_BYTES, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_Read(TMC_RA_SNAPSHOT_BYTES, &valid) == 0 && !valid);
    CHECK(TmcRaMemory_OverlayExplicit(0x7fff, explicit_bytes, sizeof(explicit_bytes)));
    CHECK(TmcRaMemory_Read(0x7fff, &valid) == 0xA5 && valid);
    CHECK(TmcRaMemory_Read(0x8000, &valid) == 0x5A && valid);
    CHECK(!TmcRaMemory_OverlayExplicit(TMC_RA_SNAPSHOT_BYTES, explicit_bytes, 1));
    CHECK(TmcRaMemory_VirtualToPhysical(0x03007fff, &physical) && physical == 0x7fff);
    CHECK(TmcRaMemory_VirtualToPhysical(0x0203ffff, &physical) && physical == 0x47fff);
    CHECK(!TmcRaMemory_VirtualToPhysical(0x04000000, &physical));
    CHECK(!TmcRaMemory_VirtualToPhysical(0x03000000, NULL));
    memset(bytes, 0xCC, sizeof(bytes)); memset(provenance, 0xCC, sizeof(provenance));
    memset(validated, 0xCC, sizeof(validated));
    CHECK(TmcRaMemory_WriteU32Le(bytes, provenance, validated, sizeof(bytes), 0, 0x12345678,
                                 TMC_RA_PROVENANCE_EXPLICIT));
    CHECK(bytes[0] == 0x78 && bytes[3] == 0x12 && provenance[2] == TMC_RA_PROVENANCE_EXPLICIT);
    CHECK(validated[0] && validated[3]);
    memset(bytes, 0xCC, sizeof(bytes));
    CHECK(!TmcRaMemory_WriteU16Le(bytes, provenance, validated, sizeof(bytes), 3, 0x1234,
                                  TMC_RA_PROVENANCE_EXPLICIT));
    CHECK(bytes[3] == 0xCC);
    CHECK(!TmcRaMemory_WriteU32Le(bytes, provenance, validated, sizeof(bytes), 1, 0x12345678,
                                  TMC_RA_PROVENANCE_EXPLICIT));
    CHECK(bytes[1] == 0xCC && bytes[3] == 0xCC);
    CHECK(!TmcRaMemory_WriteU8(NULL, provenance, validated, sizeof(bytes), 0, 1, TMC_RA_PROVENANCE_EXPLICIT));
    CHECK(TmcRaMemory_WriteTranslatedPointerLe(bytes, provenance, validated, sizeof(bytes), 0, 0x030015a0,
                                               false, TMC_RA_PROVENANCE_EXPLICIT));
    CHECK(bytes[0] == 0 && bytes[3] == 0 && provenance[0] == TMC_RA_PROVENANCE_INVALID);
    gRomData = rom; gRomSize = sizeof(rom);
    CHECK(TmcRaMemory_TranslateHostPointer(&gEwram[3], NULL, 0, 0, 0, NULL, 0, &virtual_address, NULL) &&
          virtual_address == 0x02000003);
    CHECK(TmcRaMemory_TranslateHostPointer(&gIwram[3], NULL, 0, 0, 0, NULL, 0, &virtual_address, NULL) &&
          virtual_address == 0x03000003);
    CHECK(TmcRaMemory_TranslateHostPointer(&rom[2], NULL, 0, 0, 0, NULL, 0, &virtual_address, NULL) &&
          virtual_address == 0x08000002);
    CHECK(TmcRaMemory_TranslateHostPointer(&entities[8], entities, sizeof(entities), 4, 4, NULL, 0,
                                            &virtual_address, &category) &&
          virtual_address == 0x030015a8 && category == TMC_RA_POINTER_ENTITY_POOL);
    CHECK(TmcRaMemory_TranslateHostPointer(&entities[9], NULL, 0, 0, 0, &global, 1, &virtual_address, NULL) &&
          virtual_address == 0x03001169);
    CHECK(TmcRaMemory_TranslateHostPointer(&entities[8], NULL, 0, 0, 0, &strided_global, 1,
                                            &virtual_address, NULL) &&
          virtual_address == 0x03002006);
    CHECK(!TmcRaMemory_TranslateHostPointer(&entities[9], NULL, 0, 0, 0, &strided_global, 1,
                                             &virtual_address, NULL));
    CHECK(!TmcRaMemory_TranslateHostPointer(&entities[13], NULL, 0, 0, 0, &global, 1, &virtual_address, NULL));
    CHECK(!TmcRaMemory_TranslateHostPointer(&entities[0], NULL, 0, 0, 0, NULL, 1, &virtual_address, NULL));
    CHECK(!TmcRaMemory_TranslateHostPointer(&entities[0], NULL, 0, 0, 0, &global, 1, NULL, NULL));
    CHECK(!TmcRaMemory_TranslateHostPointer((void*)1, NULL, 0, 0, 0, NULL, 0, &virtual_address, &category) &&
          category == TMC_RA_POINTER_UNKNOWN);
    CHECK(TmcRaMemory_TranslateHostPointer(NULL, NULL, 0, 0, 0, NULL, 0, &virtual_address, &category) &&
          virtual_address == 0 && category == TMC_RA_POINTER_NULL);
    gRoomControls.camera_target = &gPlayerEntity.base;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0c20] == 0x60 && TmcRaMemory_Current().bytes[0x0c23] == 0x03);
    CHECK(TmcRaMemory_Current().validated[0x0c20]);
    gRoomControls.camera_target = (Entity*)(uintptr_t)1;
    TmcRaMemory_Publish();
    CHECK(TmcRaMemory_Current().bytes[0x0c20] == 0 && TmcRaMemory_Current().bytes[0x0c23] == 0);
    CHECK(!TmcRaMemory_Current().validated[0x0c20] && !TmcRaMemory_Current().validated[0x0c23]);
    TmcRaMemory_ResetAudit();
    TmcRaMemory_ResetRequestedCoverage();
    CHECK(!TmcRaMemory_ReadBlock(0x0420, bytes, sizeof(bytes), sizeof(bytes)));
    CHECK(bytes[0] == 0 && bytes[3] == 0);
    {
        const TmcRaReadAudit audit = TmcRaMemory_ReadAudit();
        CHECK(audit.invalid_requested_ranges == 1 && audit.invalid_requested_bytes == 4);
    }
    {
        const TmcRaRequestedCoverage coverage = TmcRaMemory_RequestedCoverage();
        CHECK(coverage.selected_bytes == 4 && coverage.out_of_range_bytes == 0);
        CHECK(coverage.bitmap[0x0420] && coverage.bitmap[0x0423]);
    }
    CHECK(!TmcRaMemory_ReadBlock(TMC_RA_SNAPSHOT_BYTES, bytes, sizeof(bytes), 1));
    {
        const TmcRaRequestedCoverage coverage = TmcRaMemory_RequestedCoverage();
        CHECK(coverage.selected_bytes == 4 && coverage.out_of_range_bytes == 1);
    }
    CHECK(!TmcRaMemory_ReadBlock(0, NULL, 0, 1));
#ifdef TMC_RA_MEMORY_TEST
    TmcRaMemory_TestSetAudit((TmcRaReadAudit){ UINT32_MAX, UINT32_MAX - 1, UINT32_MAX, UINT32_MAX - 1 });
    TmcRaMemory_Read(TMC_RA_SNAPSHOT_BYTES, &valid);
    {
        const TmcRaReadAudit audit = TmcRaMemory_ReadAudit();
        CHECK(audit.requested_bytes == UINT32_MAX && audit.requested_ranges == UINT32_MAX);
        CHECK(audit.invalid_requested_bytes == UINT32_MAX && audit.invalid_requested_ranges == UINT32_MAX);
    }
#endif
    if (fails) return 1;
    puts("tmc_ra_memory_test: ALL PASS");
    return 0;
}
