#pragma once

#include "d/d_save.h"
#include "f_op/f_op_actor.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

#define dComIfG_PLAY_OFF_START_STAGE 0x3EC8u
#define dComIfG_PLAY_OFF_NEXT_STAGE  0x3ED6u

struct dComIfG_play_c {
    u8 field_0x0000[0x3EC8];
    u8 mStartStage[0x0E];
    u8 mNextStage[0x11];
    u8 field_0x3ee7[0x3F90 - 0x3EE7];
    u8 mEvent[0x4250 - 0x3F90];
    int mCameraPlay;
    u8 field_0x4254[0x4E58 - 0x4254];
    void* mCameraPtr;
    u8 field_0x4e5c[0x4E90 - 0x4E5C];
    fopAc_ac_c* mPlayer0;
    u8 field_0x4e94[0x4E9C - 0x4E94];
    fopAc_ac_c* mHorseActor;
    u8 field_0x4ea0[0x4EC6 - 0x4EA0];
    u16 mOxygen;
    u8 field_0x4ec8[0x4FA6 - 0x4EC8];
    u8 mPauseFlag;
    u8 field_0x4fa7[0x5008 - 0x4FA7];
    u32 mPlayerStatus[4][4];
};

TPHD_ASSERT_OFFSET(dComIfG_play_c, mStartStage, 0x3EC8);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mNextStage, 0x3ED6);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mEvent, 0x3F90);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mCameraPlay, 0x4250);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mCameraPtr, 0x4E58);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mPlayer0, 0x4E90);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mHorseActor, 0x4E9C);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mOxygen, 0x4EC6);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mPauseFlag, 0x4FA6);
TPHD_ASSERT_OFFSET(dComIfG_play_c, mPlayerStatus, 0x5008);

struct dComIfG_gameInfo_c {
    dSv_info_c     info;
    dComIfG_play_c play;
};

TPHD_ASSERT_OFFSET(dComIfG_gameInfo_c, play, 0x13D8);

enum {
    dComIfGp_STATUS0_ZTARGET         = 0x00200000u,
    dComIfGp_STATUS0_BOOMERANG_AIM   = 0x00080000u,
    dComIfGp_STATUS0_TRANSFORM_BLOCK = 0x08000000u,
    dComIfGp_STATUS0_UNK100000       = 0x00100000u,
};

inline dSv_info_c* dComIfGs_getSaveData(void)
{
    return (dSv_info_c*)TPHD_DATA(TPHD_DATA_ADDR_gameInfo_info);
}

inline dComIfG_play_c* dComIfGp_getPlay(void)
{
    return (dComIfG_play_c*)TPHD_DATA(TPHD_DATA_ADDR_gameInfo_play);
}

inline dComIfG_gameInfo_c* dComIfG_getGameInfo(void)
{
    return (dComIfG_gameInfo_c*)TPHD_DATA(TPHD_DATA_ADDR_gameInfo_info);
}

inline fopAc_ac_c* dComIfGp_getPlayer(void)
{
    return dComIfGp_getPlay()->mPlayer0;
}

inline fopAc_ac_c* dComIfGp_getHorseActor(void)
{
    return dComIfGp_getPlay()->mHorseActor;
}

inline bool dComIfGp_isPauseFlag(void)
{
    return dComIfGp_getPlay()->mPauseFlag != 0;
}

inline volatile u16* dComIfGp_getOxygen(void)
{
    return &dComIfGp_getPlay()->mOxygen;
}

inline u32 dComIfGp_getPlayerStatus0(u32 padId)
{
    if (padId > 3)
        return 0;
    return dComIfGp_getPlay()->mPlayerStatus[padId][0];
}

inline const char* dComIfGp_getStartStageName(void)
{
    return (const char*)dComIfGp_getPlay()->mStartStage;
}

inline bool dComIfGp_stageNameIs(const char* name)
{
    const char* cur = dComIfGp_getStartStageName();
    if (!cur || !name)
        return false;
    for (int i = 0; i < 8; ++i) {
        if (cur[i] != name[i])
            return false;
        if (name[i] == '\0')
            return true;
    }
    return true;
}

inline volatile dSv_player_status_a_c* dComIfGs_getStatusA(void)
{
    return (volatile dSv_player_status_a_c*)&dComIfGs_getSaveData()->mStatusA;
}

typedef int (*dSv_info_loadImage_t)(dSv_info_c* info, const void* image);
#define dSv_info_loadImage ((dSv_info_loadImage_t)TPHD_ADDR_dSv_info_loadImage)

typedef void (*dSv_info_prepareLoadRuntime_t)(dSv_info_c* info);
#define dSv_info_prepareLoadRuntime \
    ((dSv_info_prepareLoadRuntime_t)TPHD_ADDR_dSv_info_prepareLoadRuntime)
typedef void (*dComIfGp_itemDataInit_t)(dComIfG_play_c* play);
#define dComIfGp_itemDataInit \
    ((dComIfGp_itemDataInit_t)TPHD_ADDR_dComIfGp_itemDataInit)

typedef void (*dSv_info_getSave_t)(dSv_info_c* info, int stageNo);
#define dSv_info_getSave ((dSv_info_getSave_t)TPHD_ADDR_dSv_info_getSave)
typedef void (*dSv_info_putSave_t)(dSv_info_c* info, int stageNo);
#define dSv_info_putSave ((dSv_info_putSave_t)TPHD_ADDR_dSv_info_putSave)

inline int dComIfGs_loadImage(const void* image)
{
    return dSv_info_loadImage(dComIfGs_getSaveData(), image);
}

inline void dComIfGs_prepareLoadRuntime(void)
{
    dSv_info_prepareLoadRuntime(dComIfGs_getSaveData());
    dComIfGp_itemDataInit(dComIfGp_getPlay());
}

inline s8 dComIfGs_getStageNo(const dSv_info_c* info)
{
    return (s8)info->mDan[0];
}

inline bool dComIfGs_putSave(void)
{
    s8 stageNo = dComIfGs_getStageNo(dComIfGs_getSaveData());
    if (stageNo < 0 || stageNo >= dSv_SAVE_COUNT)
        return false;
    dSv_info_putSave(dComIfGs_getSaveData(), stageNo);
    return true;
}

inline bool dComIfGs_getSave(void)
{
    s8 stageNo = dComIfGs_getStageNo(dComIfGs_getSaveData());
    if (stageNo < 0 || stageNo >= dSv_SAVE_COUNT)
        return false;
    dSv_info_getSave(dComIfGs_getSaveData(), stageNo);
    return true;
}

inline volatile dSv_restart_c* dComIfGs_getRestart(void)
{
    return (volatile dSv_restart_c*)&dComIfGs_getSaveData()->mRestart;
}

inline bool dComIfGs_hasSaveTimestamp(void)
{
    const dSv_info_c* info = dComIfGs_getSaveData();
    return info->mSaveTimestamp[0] != 0 || info->mSaveTimestamp[1] != 0;
}

typedef int (*dDbgSave_loadRaw_t)(int);
#define dDbgSave_loadRaw ((dDbgSave_loadRaw_t)TPHD_ADDR_dDbgSave_loadRaw)

typedef void (*dDbgSave_request_t)(const char* name, const char* stage, u16 point,
                                   u8 room, u8 layer, u32 roomParam, u8 hpA, u8 hpB);
#define dDbgSave_request ((dDbgSave_request_t)TPHD_ADDR_dDbgSave_request)

inline int dDbgSave_load(const char* stem)
{
    volatile char* dst = (volatile char*)TPHD_DATA(TPHD_DATA_ADDR_dbgSaveName);
    int i = 0;
    for (; i < 0x1F && stem[i]; ++i)
        dst[i] = stem[i];
    for (; i < 0x20; ++i)
        dst[i] = '\0';
    return dDbgSave_loadRaw(0);
}
