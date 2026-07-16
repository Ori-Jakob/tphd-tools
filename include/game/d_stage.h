// game/d_stage.h -- stage / room / layer access + warping.
//
// TP queues a scene transition by filling g_dComIfG_gameInfo.play.mNextStage
// (a dStage_nextStage_c) and flipping its `enabled` byte. The game then loads
// that stage/room/spawn on the next frame. We replicate the field writes tpgz
// uses (rather than calling the engine's setter) so the warp is just memory
// writes from our thread -- no game-function call, no side effects.
//
// All addresses are TPHD Zelda.rpx (US v81), taken from Ghidra labels and the
// decompiled dStage_nextStage_c__set / dStage_startStage_c__set:
//   mStartStage (current stage)  @ 0x1014a5e8  (dStage_startStage_c)
//   mNextStage  (pending warp)   @ 0x1014a5f6  (dStage_nextStage_c)
//   mRestart.mLastMode           @ 0x101465b8
//
// dStage_startStage_c layout (stride 0xE):
//   0x0 char name[8];  0x8 s16 point(spawn);  0xA s8 roomNo;  0xB s8 layer;  0xC s8 darkArea
// dStage_nextStage_c adds, past the 0xE-byte base:
//   0xE s8 enabled;  0xF s8 wipe;  0x10 u8 wipe_speed
#pragma once

#include "game/types.h"
#include "game/f_op_actor.h"
#include "game/d_com_inf_game.h"
#include "game/d_save.h"

// ---- raw addresses ----------------------------------------------------------
#define GAME_ADDR_startStage   0x1014a5e8u   // dStage_startStage_c (current)
#define GAME_ADDR_nextStage    0x1014a5f6u   // dStage_nextStage_c  (pending)
#define GAME_ADDR_restartMode  (GAME_ADDR_restart + 0x1Cu)
// dScnPly's phase table. Ghidra: phase_1 is FUN_02ac1108 and the corresponding
// table entry at 0x10129B28 is its only data xref. The save-state loader hooks
// this slot so the restored info block is installed after old-scene teardown
// and immediately before the new scene starts reading it.
#define GAME_ADDR_dScnPly_phase1       0x02ac1108u
#define GAME_ADDR_dScnPly_phase1_slot  0x10129b28u
typedef int (*dScnPly_phase1_t)(void* scene);

// dStage_startStage_c field offsets (shared by start/next stage).
#define DSTAGE_OFF_NAME   0x0
#define DSTAGE_OFF_POINT  0x8
#define DSTAGE_OFF_ROOM   0xA
#define DSTAGE_OFF_LAYER  0xB
// dStage_nextStage_c extra fields.
#define DSTAGE_OFF_ENABLE 0xE
#define DSTAGE_OFF_WIPE   0xF
#define DSTAGE_OFF_WSPEED 0x10

#define DSTAGE_LAYER_DEFAULT  (-1)   // 0xFF: let the engine resolve the layer
#define DSTAGE_WIPE_INSTANT   13     // tpgz uses wipe 13 for an instant load
#define DSTAGE_WIPE_SPEED_INSTANT 1
#define DSTAGE_NAME_LEN       8

#ifdef __cplusplus
extern "C" {
#endif

// ---- current ("start") stage ------------------------------------------------
static inline const char* dStage_getStageName(void)
{
    return (const char*)GAME_ADDR_startStage;            // mStartStage.mName
}
static inline s16 dStage_getSpawn(void)
{
    return *(volatile s16*)(GAME_ADDR_startStage + DSTAGE_OFF_POINT);
}
static inline s8 dStage_getLayer(void)
{
    return *(volatile s8*)(GAME_ADDR_startStage + DSTAGE_OFF_LAYER);
}
static inline s8 dStage_getStartRoomNo(void)
{
    return *(volatile s8*)(GAME_ADDR_startStage + DSTAGE_OFF_ROOM);
}
static inline s8 dStage_getNextRoomNo(void)
{
    return *(volatile s8*)(GAME_ADDR_nextStage + DSTAGE_OFF_ROOM);
}
// The room Link is actually standing in (actor current.roomNo). Falls back to
// the start-stage room number when Link isn't loaded.
static inline s8 dStage_getRoomNo(void)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (link)
        return link->current.roomNo;
    return *(volatile s8*)(GAME_ADDR_startStage + DSTAGE_OFF_ROOM);
}

// ---- queue a warp -----------------------------------------------------------
// Fills mNextStage and trips `enabled` so the engine loads it next frame.
// `layer` may be DSTAGE_LAYER_DEFAULT. Mirrors tpgz's warping_menu trigger.
static inline void dStage_setNextStage(const char* name, s8 room, s16 spawn, s8 layer)
{
    volatile char* dst = (volatile char*)(GAME_ADDR_nextStage + DSTAGE_OFF_NAME);
    int i = 0;
    for (; i < DSTAGE_NAME_LEN - 1 && name[i]; ++i)
        dst[i] = name[i];
    for (; i < DSTAGE_NAME_LEN; ++i)
        dst[i] = '\0';

    *(volatile s16*)(GAME_ADDR_nextStage + DSTAGE_OFF_POINT) = spawn;
    *(volatile s8*) (GAME_ADDR_nextStage + DSTAGE_OFF_ROOM)  = room;
    *(volatile s8*) (GAME_ADDR_nextStage + DSTAGE_OFF_LAYER) = layer;
    *(volatile s8*) (GAME_ADDR_nextStage + DSTAGE_OFF_WIPE)  = DSTAGE_WIPE_INSTANT;
    *(volatile u8*) (GAME_ADDR_nextStage + DSTAGE_OFF_WSPEED) = DSTAGE_WIPE_SPEED_INSTANT;
    *(volatile u32*) GAME_ADDR_restartMode                   = 0;          // mLastMode = 0
    // enabled LAST: the engine acts the instant it sees this byte set.
    *(volatile s8*) (GAME_ADDR_nextStage + DSTAGE_OFF_ENABLE) = 1;
}

// True while a queued warp hasn't been consumed yet.
static inline bool dStage_warpPending(void)
{
    return *(volatile s8*)(GAME_ADDR_nextStage + DSTAGE_OFF_ENABLE) != 0;
}

// Force the live restart "last scene mode" to horse-start (low nibble 1). The
// recreated Link reads this directly: daAlink setStartProcInit takes
// `mLastMode & 0xF` and checkHorseStart() (FUN_0201a00c) returns true for nibble
// 1 -- which makes Link's create wait for Epona, then snap her to his spawn pos
// and force-ride. This is exactly what a real horse load-zone transition sets.
// We write it at warp-queue time (before any recreate can read mLastMode) so the
// horse-start signal isn't lost to a race with the scene rebuild; the upper bits
// (equipped item etc.) are preserved as the engine expects.
static inline void dStage_setRestartHorseStart(void)
{
    volatile dSv_restart_c* restart = dSv_getRestart();
    restart->mLastMode = (restart->mLastMode & ~0xFu) | 1u;
}

// Make the game's future void/death restart agree with an exact save-state
// position override. The restart loader uses this room/position/angle tuple
// together when it recreates Link with spawn -1.
static inline void dStage_setRestartRoomPos(const cXyz* pos, s16 angleY, s8 roomNo)
{
    volatile dSv_restart_c* restart = dSv_getRestart();
    restart->mRoomNo = roomNo;
    restart->mRoomPos.x = pos->x;
    restart->mRoomPos.y = pos->y;
    restart->mRoomPos.z = pos->z;
    restart->mRoomAngleY = angleY;
}

// Zelda.rpx helper used by dComIfGp_setNextStage before it stores mLastMode.
// It packs Link's current scene-entry state into the supplied word, including
// the held-item/action byte used to recreate an item in Link's hands after the
// transition. Starting from zero avoids inheriting a stale recovery-mode nibble.
typedef void (*daAlink_packRestartMode_t)(void* link, u32* mode);
#define daAlink_packRestartMode ((daAlink_packRestartMode_t)0x020cc518u)

static inline u32 dStage_captureRestartMode(fopAc_ac_c* link)
{
    u32 mode = 0;
    if (link)
        daAlink_packRestartMode(link, &mode);
    return mode;
}

// The engine's real "queue next stage" routine (TPHD: 0x029049f4). Unlike the
// direct field writes above (a soft scene change), this performs a full
// save-load transition that rebuilds the runtime from the save block -- needed
// after restoring a save-state so inventory/flags/hearts take effect. Args
// mirror dusklight's State Share (noVisit=1 / setPoint=1) except the wipe speed.
//
// wipeSpeedT picks the fade-out duration in FRAMES from l_wipeSpeedTable @
// 0x1008b81c {_, 13, 6, 1}; out-of-range -> 26. The fader (FUN_02a917d0) loads
// this into a per-frame countdown, so a SMALLER number = FASTER wipe. dusklight
// uses 3 (-> 1 frame = near-instant), which is too abrupt for a real load (it
// can reveal the scene before it's settled). We use 0 (-> 26 frames, the normal
// game transition) so loads fade at the standard, safer speed.
#define DSTAGE_WIPESPEEDT_LOAD 0   // 0 -> wipe_speed 26 (~0.9s); 3 -> 1 (instant)

typedef void (*dComIfGp_setNextStage_t)(const char* stage, s16 spawn, s8 room, s8 layer,
                                        f32 lastSpeed, u32 lastMode, int setPoint, s8 wipe,
                                        s16 lastAngle, int noVisit, int wipeSpeedT);
#define dComIfGp_setNextStage ((dComIfGp_setNextStage_t)0x029049f4u)

static inline void dStage_loadStage(const char* name, s8 room, s16 spawn, s8 layer)
{
    dComIfGp_setNextStage(name, spawn, room, layer, 0.0f, 0, 1, 0, 0, 1,
                          DSTAGE_WIPESPEEDT_LOAD);
}

// Queue a memfile/save-state transition the way tpgz does: establish the saved
// restart point, fill mNextStage directly, then publish `enabled` last. This
// avoids deriving field-last-stay, item-start, and other transition metadata
// from the outgoing scene immediately before phase_1 replaces dSv_info_c.
static inline void dStage_loadStateDirect(const char* name, s8 room, s16 spawn, s8 layer)
{
    dSv_getRestart()->mStartPoint = spawn;
    dStage_setNextStage(name, room, spawn, layer);
}

// Normal file-select resume transition, matching FUN_029076e4. Unlike the
// save-state transition above, noVisit is false and the destination comes from
// dSv_player_return_place_c.
static inline void dStage_loadSavedGame(const char* name, s8 room, s16 spawn)
{
    dComIfGp_setNextStage(name, spawn, room, DSTAGE_LAYER_DEFAULT,
                          0.0f, 0, 1, 0, 0, 0, 0);
}

// ---- override Link's position/facing (used after a save-state load) ---------
// Also corrects his room (so collision queries the right bg) and zeroes his
// velocity (so he doesn't carry inherited fall speed into the new spot and clip
// through the floor on the next physics step). Call only after the reload has
// settled and Link has run at least one update frame.
static inline void dStage_setLinkPos(const cXyz* pos, s16 angleY, s8 roomNo)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    link->current.pos    = *pos;
    link->old.roomNo     = roomNo;
    link->current.roomNo = roomNo;
    link->shape_angle.y  = angleY;
    link->speed.x = link->speed.y = link->speed.z = 0.0f;
    link->speedF  = 0.0f;
}

#ifdef __cplusplus
}
#endif
