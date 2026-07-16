// game/d_save.h -- the persistent game-info / save block (dSv_info_c).
//
// There is ONE player-status block, not two. The struct gameplay reads/writes
// each frame for hearts/rupees (what the HUD displays) IS the head of the
// persistent save block g_dComIfG_gameInfo.info @ 0x10145348 -- the same bytes
// the game serializes on normal saves.
#pragma once

#include "game/types.h"
#include "game/d_com_inf_game.h"   // GAME_ADDR_gameInfo_info, GAME_DSVINFO_SIZE

// ---- player status at the head of the info block (@ 0x10145348) -------------
// info.mSavedata.mPlayer.mStatusA, with a 2-byte lead-in before mMaxLife (so
// mMaxLife is at info+0x02 -> 0x1014534A, matching the verified addresses).
typedef struct dSv_player_status_a_c {
    u8  field_0x00[0x02];
    u16 mMaxLife;   // 0x02 -> 0x1014534A
    u16 mLife;      // 0x04 -> 0x1014534C  (full = (mMaxLife / 5) * 4)
    u16 mRupee;     // 0x06 -> 0x1014534E
} dSv_player_status_a_c;

#define g_meterInfo ((volatile dSv_player_status_a_c*)GAME_ADDR_gameInfo_info)

// ---- save-image timestamp ---------------------------------------------------
// FUN_02aa8ecc writes OSGetTime() here immediately before serializing a normal
// save. A newly initialized file-select image is zero until it has actually
// been saved. The old external autosplitter called this "PlaytimeTimer", but it
// is a 64-bit save timestamp, not elapsed gameplay time.
#define DSV_INFO_SAVE_TIMESTAMP_OFF 0x28u
#define GAME_ADDR_saveTimestamp \
    (GAME_ADDR_gameInfo_info + DSV_INFO_SAVE_TIMESTAMP_OFF)

static inline bool dSv_hasSaveTimestamp(void)
{
    return *(volatile u32*)(GAME_ADDR_saveTimestamp + 0) != 0 ||
           *(volatile u32*)(GAME_ADDR_saveTimestamp + 4) != 0;
}

// ---- engine save-load helpers ----------------------------------------------
// FUN_02aa8af8 is the game's "card/debug save -> live memory" deserializer. It
// copies the serialized save image into g_dComIfG_gameInfo.info, then performs
// the same post-load normalization the file-select/debug-save path does: clamp
// minimum life, fix item slots, update vibration/camera config, line up items,
// and refresh the meter save-stage name. The serialized portion is 0xDF8 bytes;
// our save-state file keeps the larger info snapshot too, but cold loads should
// enter the scene through this helper first so title-screen runtime is initialized.
#define GAME_DSV_SERIALIZED_SIZE 0x0DF8u

typedef int (*dSv_info_loadImage_t)(void* info, const void* image);
#define dSv_info_loadImage ((dSv_info_loadImage_t)0x02aa8af8u)

// FUN_02aa84d0 / FUN_0290a3e8 are called by the game's own debug-save request
// consumer before it reads the .dat. Together they clear transient save/meter
// state and rebuild dComIfG_play_c item/HUD counters from a sane baseline.
typedef void (*dSv_info_prepareLoadRuntime_t)(void* info);
#define dSv_info_prepareLoadRuntime ((dSv_info_prepareLoadRuntime_t)0x02aa84d0u)

typedef void (*dComIfGp_itemDataInit_t)(void* play);
#define dComIfGp_itemDataInit ((dComIfGp_itemDataInit_t)0x0290a3e8u)

static inline void dSave_prepareLoadRuntime(void)
{
    dSv_info_prepareLoadRuntime((void*)GAME_ADDR_gameInfo_info);
    dComIfGp_itemDataInit((void*)GAME_ADDR_gameInfo_play);
}

static inline int dSave_loadImage(const void* image)
{
    return dSv_info_loadImage((void*)GAME_ADDR_gameInfo_info, image);
}

// ---- rebuild the live current-stage save record -----------------------------
// A full dSv_info_c snapshot contains both:
//   mSavedata.mSave[32] @ info+0x2F0 (persistent per-stage records), and
//   mMemory             @ info+0xDF8 (the live current-stage record).
//
// tpgz's phase_1 memfile injector calls dComIfGs_getSave(mDan.mStageNo)
// immediately after its full-block memcpy. TPHD's equivalent is
// FUN_02aa8520: it copies mSavedata.mSave[stageNo] to mMemory. Ghidra confirms
// mDan.mStageNo at info+0xE18 and the selected-record stride of 0x20.
#define DSV_INFO_STAGE_RECORDS_OFF 0x02F0u
#define DSV_STAGE_RECORD_SIZE      0x0020u
#define DSV_STAGE_RECORD_COUNT     32
#define DSV_INFO_MEMORY_OFF        0x0DF8u
#define DSV_INFO_MEMORY_SIZE       DSV_STAGE_RECORD_SIZE
#define DSV_INFO_DAN_OFF           0x0E18u
#define DSV_INFO_ZONE_OFF          0x0E54u
#define DSV_INFO_ZONE_SIZE         0x0400u
#define DSV_INFO_DAN_SIZE          (DSV_INFO_ZONE_OFF - DSV_INFO_DAN_OFF)
#define DSV_INFO_DAN_STAGENO_OFF   DSV_INFO_DAN_OFF

typedef void (*dSv_info_getSave_t)(void* info, int stageNo);
#define dSv_info_getSave ((dSv_info_getSave_t)0x02aa8520u)

// FUN_02aa856c is the inverse operation used by dComIfGs_putSave: copy live
// mMemory back to mSavedata.mSave[stageNo]. tpgz performs this before capturing
// a memfile so the serialized stage record cannot lag behind live room flags.
typedef void (*dSv_info_putSave_t)(void* info, int stageNo);
#define dSv_info_putSave ((dSv_info_putSave_t)0x02aa856cu)

static inline s8 dSave_getStageNo(const void* info)
{
    return *(const s8*)((const u8*)info + DSV_INFO_DAN_STAGENO_OFF);
}

static inline bool dSave_commitCurrentStageMemory(void)
{
    s8 stageNo = dSave_getStageNo((const void*)GAME_ADDR_gameInfo_info);
    if (stageNo < 0 || stageNo >= DSV_STAGE_RECORD_COUNT)
        return false;
    dSv_info_putSave((void*)GAME_ADDR_gameInfo_info, stageNo);
    return true;
}

static inline bool dSave_rebuildCurrentStageMemory(void)
{
    s8 stageNo = dSave_getStageNo((const void*)GAME_ADDR_gameInfo_info);
    if (stageNo < 0 || stageNo >= DSV_STAGE_RECORD_COUNT)
        return false;
    dSv_info_getSave((void*)GAME_ADDR_gameInfo_info, stageNo);
    return true;
}

// ---- Link's oxygen/air (in the play struct) ---------------------------------
#define GAME_ADDR_oxygen 0x1014B5E6u
static inline volatile u16* dComIfGp_getOxygen(void)
{
    return (volatile u16*)GAME_ADDR_oxygen;
}

// ---- debug-save loader (content DebugSaves/<name>.dat) ----------------------
// The game ships dev save files in /vol/content/DebugSaves/*.dat (ZTP00.dat,
// COMMON.dat, ...). FUN_02abc50c @ 0x02abc50c reads DebugSaves/<name>.dat (name
// = the C-string @ 0x1017b308, the "%s" in "DebugSaves/%s.dat"), validates it,
// and deserializes it into the info block @ 0x10145348 (FUN_02aa8af8). Returns
// 1 on success, 2 on failure; its int arg is unused. After this the runtime
// needs a stage reload for the loaded flags/inventory to take effect.
#define GAME_ADDR_dbgSaveName 0x1017b308u   // char[0x20]
typedef int (*dDbgSave_loadRaw_t)(int);
#define dDbgSave_loadRaw ((dDbgSave_loadRaw_t)0x02abc50cu)

static inline int dDbgSave_load(const char* stem)
{
    volatile char* dst = (volatile char*)GAME_ADDR_dbgSaveName;
    int i = 0;
    for (; i < 0x1F && stem[i]; ++i)
        dst[i] = stem[i];
    for (; i < 0x20; ++i)
        dst[i] = '\0';
    return dDbgSave_loadRaw(0);
}

// The game's FULL "load a debug save the way the dev menu does" request
// (FUN_02abceb0 @ 0x02abceb0). dDbgSave_load above only reads the .dat into info;
// this drives the COMPLETE path: it stashes the request (save name + target
// stage/point/room/layer) and kicks a scene change into the debug-load scene,
// which reads the .dat AND warps AND rebuilds the HUD/meter (FUN_02aa84d0) AND
// fixes restart data -- everything a normal load does, in the right order. Doing
// the read + a hand-rolled warp ourselves skips the meter rebuild, which crashes
// the HUD on a half-built Link. `name` carries the subfolder prefix
// ("QASaves/..."); `stage` is an OPTIONAL hard destination override. Passing a
// non-empty stage bypasses normal file-select resume rules (including dungeon
// entrance placement); pass null/empty to let the loaded save's restart data
// choose the location. Mirrors the QA smoke test's explicit-stage call:
//   dDbgSave_request("QASaves/ZTP85", "D_SB11", 0, room, 0xFF, 0, 0, 0).
typedef void (*dDbgSave_request_t)(const char* name, const char* stage, u16 point,
                                   u8 room, u8 layer, u32 roomParam, u8 hpA, u8 hpB);
#define dDbgSave_request ((dDbgSave_request_t)0x02abceb0u)

// A debug .dat is a raw dump of the info block: the loader's deserialize is just
// memcpy(&info, fileBuf, 0xDF8). The return-place block at info/file+0x58 is
// what normal file-select loading uses for its destination:
//   name @ 0x58, spawn/playerStatus @ 0x60, room @ 0x61.
// FUN_029076e4 passes those fields directly to dComIfGp_setNextStage.
#define DSV_DAT_RETURN_PLACE_OFF 0x58u
#define DSV_RETURN_PLACE_OFF_NAME 0x00u
#define DSV_RETURN_PLACE_OFF_POINT 0x08u
#define DSV_RETURN_PLACE_OFF_ROOM 0x09u

typedef struct dSv_player_return_place_c {
    char mName[8];
    u8   mPlayerStatus; // used as the next-stage point/spawn
    s8   mRoomNo;
    u8   field_0x0a[2];
} dSv_player_return_place_c;

static_assert(sizeof(dSv_player_return_place_c) == 0x0C,
              "dSv_player_return_place_c layout changed");

#define GAME_ADDR_playerReturnPlace \
    (GAME_ADDR_gameInfo_info + DSV_DAT_RETURN_PLACE_OFF)

static inline const dSv_player_return_place_c* dSv_getPlayerReturnPlace(void)
{
    return (const dSv_player_return_place_c*)GAME_ADDR_playerReturnPlace;
}

// Field-last-stay remains useful historical field metadata, but is not the
// normal load destination.
#define DSV_DAT_LASTSTAY_OFF 0x64u

// ---- field "last stay" = historical field position (in the info block) -------
// dSv_player_field_last_stay_info_c @ info+0x64. dComIfGp_setNextStage writes it
// as {pos, angle, name = current stage, room = Link's current.roomNo, region}.
// It is not always the normal resume destination: dungeon saves may retain the
// surrounding field here while file select deliberately resumes at the dungeon
// entrance. Layout: cXyz pos@0x00, s16 angle@0x0C,
// char name[8]@0x0E, s8 room@0x16, u8 regionNo@0x17, u8 existFlag@0x18.
#define GAME_ADDR_fieldLastStay (GAME_ADDR_gameInfo_info + DSV_DAT_LASTSTAY_OFF)  // 0x101453ac
#define DSV_LASTSTAY_OFF_ANGLE 0x0Cu
#define DSV_LASTSTAY_OFF_NAME  0x0Eu
#define DSV_LASTSTAY_OFF_ROOM  0x16u

static inline const char* dSv_lastStayStage(void)
{
    return (const char*)(GAME_ADDR_fieldLastStay + DSV_LASTSTAY_OFF_NAME);
}
static inline s8 dSv_lastStayRoom(void)
{
    return *(volatile s8*)(GAME_ADDR_fieldLastStay + DSV_LASTSTAY_OFF_ROOM);
}
static inline s16 dSv_lastStayAngle(void)
{
    return *(volatile s16*)(GAME_ADDR_fieldLastStay + DSV_LASTSTAY_OFF_ANGLE);
}
static inline const cXyz* dSv_lastStayPos(void)
{
    return (const cXyz*)GAME_ADDR_fieldLastStay;
}
static inline bool dSv_hasLastStay(void)
{
    return dSv_lastStayStage()[0] != '\0';
}

// ---- void/death restart state ----------------------------------------------
// dSv_restart_c @ info+0x1254. Ghidra's dStage_playerInit and the void-restart
// path confirm that a restart with spawn -1 uses this room together with
// mRoomPos/mRoomAngleY. Keep the tuple coherent whenever a save-state position
// override moves Link, otherwise a later void can load a room with coordinates
// belonging to another room and leave the transition on a black screen.
#define DSV_INFO_RESTART_OFF 0x1254u
#define GAME_ADDR_restart   (GAME_ADDR_gameInfo_info + DSV_INFO_RESTART_OFF)

typedef struct dSv_restart_c {
    s8   mRoomNo;       // 0x00: room loaded by the void/death restart path
    u8   field_0x01[3];
    s16  mStartPoint;    // 0x04
    s16  mRoomAngleY;    // 0x06
    cXyz mRoomPos;       // 0x08
    u32  mRoomParam;     // 0x14
    f32  mLastSpeedF;    // 0x18
    u32  mLastMode;      // 0x1C
    s16  mLastAngleY;    // 0x20
    u8   field_0x22[2];
} dSv_restart_c;

static_assert(sizeof(dSv_restart_c) == 0x24, "dSv_restart_c layout changed");

static inline volatile dSv_restart_c* dSv_getRestart(void)
{
    return (volatile dSv_restart_c*)GAME_ADDR_restart;
}

static inline dSv_restart_c* dSv_getRestartFromInfo(void* info)
{
    return (dSv_restart_c*)((u8*)info + DSV_INFO_RESTART_OFF);
}

static inline const dSv_restart_c* dSv_getRestartFromInfo(const void* info)
{
    return (const dSv_restart_c*)((const u8*)info + DSV_INFO_RESTART_OFF);
}
