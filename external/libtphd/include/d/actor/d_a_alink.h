#pragma once

#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/actor/d_a_player.h"
#include "d/d_event.h"
#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct daAlink_c : daPy_py_c {
    u8 field_0x060c[0x062C - 0x060C];
    void* field_0x062c;
    u8 field_0x0630[0x19AC - 0x0630];
    u32 mLinkAcchFlags;
    u8 field_0x19b0[0x2828 - 0x19B0];
    fopAc_ac_c* mRideActor;
    u8 field_0x282c[0x285C - 0x282C];
    fopAc_ac_c* mItemAcKeep;
    u8 field_0x2860[0x286C - 0x2860];
    fopAc_ac_c* mThrowBoomerangAcKeep;
    u8 field_0x2870[0x3009 - 0x2870];
    u8  field_0x3009;
    u8  field_0x300a[0x301A - 0x300A];
    u8  mRideStatus;
    u8  field_0x301b[0x304C - 0x301B];
    u16 mEquipItem;
    u8  field_0x304e[0x305C - 0x304E];
    u16 mProcID;
    u8  field_0x305e[0x3218 - 0x305E];
    u32 mModeFlg;
    u8  field_0x321c[0x3458 - 0x321C];
    f32 mNormalSpeed;
    u8  field_0x345c[0x37B0 - 0x345C];
    f32 mMagneGroundNormalY;
};

TPHD_ASSERT_OFFSET(daAlink_c, mLinkAcchFlags, 0x19AC);
TPHD_ASSERT_OFFSET(daAlink_c, mItemAcKeep, 0x285C);
TPHD_ASSERT_OFFSET(daAlink_c, mThrowBoomerangAcKeep, 0x286C);
TPHD_ASSERT_OFFSET(daAlink_c, mRideStatus, 0x301A);
TPHD_ASSERT_OFFSET(daAlink_c, mEquipItem, 0x304C);
TPHD_ASSERT_OFFSET(daAlink_c, mProcID, 0x305C);
TPHD_ASSERT_OFFSET(daAlink_c, mModeFlg, 0x3218);
TPHD_ASSERT_OFFSET(daAlink_c, mNormalSpeed, 0x3458);
TPHD_ASSERT_OFFSET(daAlink_c, mMagneGroundNormalY, 0x37B0);

enum {
    daAlink_PROC_WAIT       = 0x03,
    daAlink_PROC_FRONT_ROLL = 0x0E,
    daAlink_PROC_DIVE_JUMP  = 0x16,
    daAlink_PROC_CRAWL_END  = 0x38,
};

enum {
    daAlink_RIDE_NONE  = 0,
    daAlink_RIDE_HORSE = 1,
    daAlink_RIDE_BOAR  = 2,
};

enum {
    daAlink_MODE_FLY      = 0x00070C52u,
    daAlink_MODE_NOGNDCHK = 0x00040000u,
};

enum {
    daAlink_ACCH_GROUNDHIT = 0x20u,
};

#define daAlink_ITEM_IRONBALL 0x42u

typedef int (*daAlink_procWaitInit_t)(daAlink_c* link);
#define daAlink_procWaitInit ((daAlink_procWaitInit_t)TPHD_ADDR_daAlink_procWaitInit)

typedef int (*daAlink_procCoMetamorphoseInit_t)(daAlink_c* link, u8 param);
#define daAlink_procCoMetamorphoseInit \
    ((daAlink_procCoMetamorphoseInit_t)TPHD_ADDR_daAlink_procCoMetamorphoseInit)

typedef void (*daAlink_packRestartMode_t)(daAlink_c* link, u32* mode);
#define daAlink_packRestartMode \
    ((daAlink_packRestartMode_t)TPHD_ADDR_daAlink_packRestartMode)

typedef int (*daAlink_checkSpecialEnv_t)(void);
#define daAlink_checkSpecialEnv \
    ((daAlink_checkSpecialEnv_t)TPHD_ADDR_daAlink_checkSpecialEnv)

typedef int (*daMidna_checkMetamorphoseEnableBase_t)(void);
#define daMidna_checkMetamorphoseEnableBase \
    ((daMidna_checkMetamorphoseEnableBase_t)TPHD_ADDR_daMidna_checkMetamorphoseEnableBase)

typedef int (*cBgW_CheckBGround_t)(double y);
#define cBgW_CheckBGround ((cBgW_CheckBGround_t)TPHD_ADDR_cBgW_CheckBGround)

typedef int (*dComIfG_getLayerNo_t)(int pad);
#define dComIfG_getLayerNo ((dComIfG_getLayerNo_t)TPHD_ADDR_dComIfG_getLayerNo)

inline u16 daAlink_getProcID(const daAlink_c* link) { return link->mProcID; }
inline u16 daAlink_getEquipItem(const daAlink_c* link) { return link->mEquipItem; }

inline bool daAlink_isHorseRiding(const daAlink_c* link)
{
    return link && link->mRideStatus == daAlink_RIDE_HORSE;
}

inline bool daAlink_onGround(const daAlink_c* link)
{
    return link && (link->mLinkAcchFlags & daAlink_ACCH_GROUNDHIT) != 0;
}

inline void daAlink_clearMomentum(daAlink_c* link)
{
    if (!link)
        return;
    link->speed.zero();
    link->speedF = 0.0f;
    link->mNormalSpeed = 0.0f;
}

inline void daAlink_setPos(daAlink_c* link, const cXyz* pos, s16 angleY, s8 roomNo)
{
    if (!link || !pos)
        return;
    link->current.pos = *pos;
    link->old.roomNo = roomNo;
    link->current.roomNo = roomNo;
    link->old.angle.y = angleY;
    link->current.angle.y = angleY;
    link->shape_angle.y = angleY;
    daAlink_clearMomentum(link);
}

inline u32 daAlink_captureRestartMode(daAlink_c* link)
{
    u32 mode = 0;
    if (link)
        daAlink_packRestartMode(link, &mode);
    return mode;
}

inline void daAlink_playTransformConfirmSE(void)
{
    *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_transformSeAck) = 1;
    Z2SeStart(Z2SE_WARP_MAP_DECIDE);
}

inline void daAlink_playErrorSE(void)
{
    Z2SeStart(Z2SE_SYS_ERROR);
}

inline bool daAlink_canTransform(daAlink_c* link)
{
    if (!link)
        return false;

    const u32 playStatus0 = dComIfGp_getPlayerStatus0(0);
    const bool magne = (link->mNoResetFlg1 & daPy_STATUS_MAGNE) != 0;

    if (link->mEquipItem == daAlink_ITEM_IRONBALL)
        return false;

    if (link->mThrowBoomerangAcKeep != nullptr)
        return false;
    if ((playStatus0 & dComIfGp_STATUS0_BOOMERANG_AIM) != 0 &&
        link->mItemAcKeep != nullptr)
        return false;

    if (link->mRideStatus != daAlink_RIDE_HORSE &&
        link->mRideStatus != daAlink_RIDE_BOAR &&
        (link->mModeFlg & daAlink_MODE_NOGNDCHK) == 0 && !magne) {
        if ((link->mLinkAcchFlags & daAlink_ACCH_GROUNDHIT) == 0)
            return false;
        if ((link->mModeFlg & daAlink_MODE_FLY) != 0)
            return false;
    }

    if ((playStatus0 & dComIfGp_STATUS0_TRANSFORM_BLOCK) != 0)
        return false;
    if (link->mProcID == daAlink_PROC_CRAWL_END)
        return false;
    if ((link->mNoResetFlg3 & daPy_STATUS3_BLOCK) != 0)
        return false;
    if (daAlink_checkSpecialEnv())
        return false;
    if (dComIfGp_stageNameIs("D_MN08A"))
        return false;
    if (dComIfGp_stageNameIs("D_MN09A")) {
        const s8 room = link->current.roomNo;
        if (room == 51)
            return false;
        if (room == 50 && dComIfG_getLayerNo(0) == 0)
            return false;
    }
    if (magne) {
        if (!dComIfGp_stageNameIs("D_MN04B"))
            return false;
        if (!cBgW_CheckBGround((double)link->mMagneGroundNormalY))
            return false;
    }
    if (dComIfGs_isEventBit(dEvent_F0800_BYTE, dEvent_F0800_MASK))
        return false;
    if ((link->mNoResetFlg1 & daPy_STATUS_MIDNA) == 0)
        return false;

    if (link->mDemo.mDemoMode != 0)
        return false;
    if (link->mRideStatus != daAlink_RIDE_NONE && link->mRideStatus != 5)
        return false;
    if ((playStatus0 & dComIfGp_STATUS0_UNK100000) != 0)
        return false;
    if ((link->mNoResetFlg1 & daPy_STATUS_UNK800000) != 0)
        return false;
    if (dComIfGp_isPauseFlag() || dEvt_isEventRunning())
        return false;
    if (!daMidna_checkMetamorphoseEnableBase())
        return false;

    return true;
}

inline bool daAlink_quickTransform(daAlink_c* link)
{
    if (!daAlink_canTransform(link)) {
        daAlink_playErrorSE();
        return false;
    }
    if (!daAlink_procCoMetamorphoseInit(link, 1)) {
        daAlink_playErrorSE();
        return false;
    }
    daAlink_playTransformConfirmSE();
    return true;
}
