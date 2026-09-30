#pragma once

#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

#define dStage_NAME_LEN 8

struct dStage_startStage_c {
    char mName[dStage_NAME_LEN];
    s16  mPoint;
    s8   mRoomNo;
    s8   mLayer;
    s8   mDarkArea;
    u8   field_0x0d;
};

TPHD_ASSERT_OFFSET(dStage_startStage_c, mPoint, 0x08);
TPHD_ASSERT_OFFSET(dStage_startStage_c, mRoomNo, 0x0A);
TPHD_ASSERT_SIZE(dStage_startStage_c, 0x0E);

struct dStage_nextStage_c : dStage_startStage_c {
    s8 mEnabled;
    s8 mWipe;
    u8 mWipeSpeed;
    u8 field_0x11;
};

TPHD_ASSERT_OFFSET(dStage_nextStage_c, mEnabled, 0x0E);
TPHD_ASSERT_SIZE(dStage_nextStage_c, 0x12);

#define dStage_LAYER_DEFAULT      (-1)
#define dStage_WIPE_INSTANT       13
#define dStage_WIPE_SPEED_INSTANT 1

#define dStage_ROOM_STATUS_STRIDE 0x0404u
#define dStage_ROOM_COUNT         64

#define dStage_ROOM_STATUS_BG_READY 0x10u

inline dStage_startStage_c* dComIfGp_getStartStage(void)
{
    return (dStage_startStage_c*)dComIfGp_getPlay()->mStartStage;
}

inline dStage_nextStage_c* dComIfGp_getNextStage(void)
{
    return (dStage_nextStage_c*)dComIfGp_getPlay()->mNextStage;
}

inline const char* dStage_getStageName(void)
{
    return dComIfGp_getStartStage()->mName;
}

inline bool dStage_nameIs(const char* name)
{
    const char* cur = dStage_getStageName();
    if (!cur || !name)
        return false;
    for (int i = 0; i < dStage_NAME_LEN; ++i) {
        if (cur[i] != name[i])
            return false;
        if (name[i] == '\0')
            return true;
    }
    return true;
}

inline s16 dStage_getSpawn(void) { return dComIfGp_getStartStage()->mPoint; }
inline s8 dStage_getLayer(void) { return dComIfGp_getStartStage()->mLayer; }
inline s8 dStage_getStartRoomNo(void) { return dComIfGp_getStartStage()->mRoomNo; }
inline s8 dStage_getNextRoomNo(void) { return dComIfGp_getNextStage()->mRoomNo; }

inline s8 dStage_getRoomNo(void)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    return link ? link->current.roomNo : dComIfGp_getStartStage()->mRoomNo;
}

inline void dStage_setCurrentInfo(s8 room, s16 spawn, s8 layer)
{
    dStage_startStage_c* stage = dComIfGp_getStartStage();
    stage->mPoint = spawn;
    stage->mRoomNo = room;
    stage->mLayer = layer;
}

inline void dStage_setLinkRoom(s8 roomNo)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    link->old.roomNo = roomNo;
    link->current.roomNo = roomNo;
}

inline s8 dStage_getStayRoomNo(void)
{
    return *(volatile s8*)TPHD_DATA(TPHD_DATA_ADDR_roomControl_stayNo);
}

inline u8 dStage_getRoomStatusFlags(s8 roomNo)
{
    if (roomNo < 0 || roomNo >= dStage_ROOM_COUNT)
        return 0;
    return *(volatile u8*)(TPHD_DATA(TPHD_DATA_ADDR_roomControl_statusFlag) +
                           (u32)roomNo * dStage_ROOM_STATUS_STRIDE);
}

inline bool dStage_isRoomBackgroundReady(s8 roomNo)
{
    return (dStage_getRoomStatusFlags(roomNo) & dStage_ROOM_STATUS_BG_READY) != 0;
}

inline s8 dStage_getRoomZoneNo(s8 roomNo)
{
    if (roomNo < 0 || roomNo >= dStage_ROOM_COUNT)
        return -1;
    return *(volatile s8*)(TPHD_DATA(TPHD_DATA_ADDR_roomControl_zoneNo) +
                           (u32)roomNo * dStage_ROOM_STATUS_STRIDE);
}

inline void dStage_setRoomReadId(s8 roomNo)
{
    *(volatile s8*)TPHD_DATA(TPHD_DATA_ADDR_roomControl_roomReadId) = roomNo;
}

typedef int (*dStage_RoomCheck_t)(void* groundCheck);
#define dStage_RoomCheck ((dStage_RoomCheck_t)TPHD_ADDR_dStage_RoomCheck)

inline int dStage_requestRoom(s8 roomNo)
{
    dStage_setRoomReadId(roomNo);
    return dStage_RoomCheck(nullptr);
}

inline void dStage_clearRoomRequest(void)
{
    dStage_setRoomReadId(-1);
}

inline void dStage_setNextStage(const char* name, s8 room, s16 spawn, s8 layer)
{
    dStage_nextStage_c* next = dComIfGp_getNextStage();

    int i = 0;
    for (; i < dStage_NAME_LEN - 1 && name[i]; ++i)
        next->mName[i] = name[i];
    for (; i < dStage_NAME_LEN; ++i)
        next->mName[i] = '\0';

    next->mPoint = spawn;
    next->mRoomNo = room;
    next->mLayer = layer;
    next->mWipe = dStage_WIPE_INSTANT;
    next->mWipeSpeed = dStage_WIPE_SPEED_INSTANT;
    dComIfGs_getSaveData()->mRestart.mLastMode = 0;
    next->mEnabled = 1;
}

inline void dStage_requestReload(void)
{
    dComIfGp_getNextStage()->mEnabled = 1;
}

inline bool dStage_warpPending(void)
{
    return dComIfGp_getNextStage()->mEnabled != 0;
}

inline void dStage_setRestartHorseStart(void)
{
    volatile dSv_restart_c* restart =
        (volatile dSv_restart_c*)&dComIfGs_getSaveData()->mRestart;
    restart->mLastMode = (restart->mLastMode & ~0xFu) | 1u;
}

inline void dStage_setRestartRoomPos(const cXyz* pos, s16 angleY, s8 roomNo)
{
    volatile dSv_restart_c* restart =
        (volatile dSv_restart_c*)&dComIfGs_getSaveData()->mRestart;
    restart->mRoomNo = roomNo;
    restart->mRoomPos.x = pos->x;
    restart->mRoomPos.y = pos->y;
    restart->mRoomPos.z = pos->z;
    restart->mRoomAngleY = angleY;
}

typedef void (*dComIfGp_setNextStage_t)(const char* stage, s16 spawn, s8 room,
                                        s8 layer, f32 lastSpeed, u32 lastMode,
                                        int setPoint, s8 wipe, s16 lastAngle,
                                        int noVisit, int wipeSpeedT);
#define dComIfGp_setNextStage \
    ((dComIfGp_setNextStage_t)TPHD_ADDR_dComIfGp_setNextStage)

#define dStage_WIPESPEEDT_LOAD 0

inline void dStage_loadStage(const char* name, s8 room, s16 spawn, s8 layer)
{
    dComIfGp_setNextStage(name, spawn, room, layer, 0.0f, 0, 1, 0, 0, 1,
                          dStage_WIPESPEEDT_LOAD);
}

inline void dStage_loadSavedGame(const char* name, s8 room, s16 spawn)
{
    dComIfGp_setNextStage(name, spawn, room, dStage_LAYER_DEFAULT, 0.0f, 0, 1, 0,
                          0, 0, 0);
}

inline void dStage_loadStateDirect(const char* name, s8 room, s16 spawn, s8 layer)
{
    dComIfGs_getSaveData()->mRestart.mStartPoint = spawn;
    dStage_setNextStage(name, room, spawn, layer);
}

typedef int (*dScnPly_phase1_t)(void* scene);
typedef int (*dScnRoom_zone_phase_t)(void* roomScene);

#define dScnRoom_OFF_ROOMNO 0x00B0u
