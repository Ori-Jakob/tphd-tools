#pragma once

#include "SSystem/SComponent/c_angle.h"
#include "SSystem/SComponent/c_xyz.h"
#include "d/d_cam_param.h"
#include "d/d_com_inf_game.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

struct dCamera_xform_c {
    cXyz mAt;
    cXyz mEye;
};

TPHD_ASSERT_SIZE(dCamera_xform_c, 0x18);

struct dCamera_stick_c {
    f32 mLastX;
    f32 mLastY;
    f32 mMagnitude;
    f32 mDX;
    f32 mDY;
    f32 field_0x14;
    s16 mAngle;
    u8  field_0x1a[2];
};

TPHD_ASSERT_SIZE(dCamera_stick_c, 0x1C);

struct dCamera_c {
    u8 field_0x0000[0x30];
    dCamera_xform_c mCurrent;
    u8 field_0x0048[0x5C - 0x48];
    cSGlobe mViewDir;
    dCamera_xform_c mViewCache;
    u8 field_0x007c[4];
    f32 mFovy;
    u8 field_0x0084[0x17C - 0x84];
    u32 mPadId;
    u8 field_0x0180[0x190 - 0x180];
    int mIsWolf;
    u8 field_0x0194[0x1A4 - 0x194];
    int mCurMode;
    u8 field_0x01a8[0x1BC - 0x1A8];
    dCamera_stick_c mMainStick;
    dCamera_stick_c mCStick;
    int mCStickYState;
    int mCStickYHoldCount;
    int mCStickUpLatch;
    u8 field_0x0200[0x684 - 0x200];
    dCamType_c* mCamTypeData;
    int mCamTypeCount;
    int mCurType;
    u8 field_0x0690[4];
    int mDefaultType;
    u8 field_0x0698[0x7AC - 0x698];
    dCamMapToolData_c mRoomMapTool;
    u8 field_0x07e8[0x860 - 0x7E8];
    dCamMapToolData_c mTagCamTool;
    u8 field_0x089c[0x940 - 0x89C];
    int mGear;
    u8 field_0x0944[0xB38 - 0x944];
    dCamParam_c* mCamStyleData;
    u8 field_0x0b3c[4];
    dCamParam_c* mCurStyle;
    int mCurStyleIndex;
};

TPHD_ASSERT_OFFSET(dCamera_c, mCurrent, 0x0030);
TPHD_ASSERT_OFFSET(dCamera_c, mViewDir, 0x005C);
TPHD_ASSERT_OFFSET(dCamera_c, mViewCache, 0x0064);
TPHD_ASSERT_OFFSET(dCamera_c, mFovy, 0x0080);
TPHD_ASSERT_OFFSET(dCamera_c, mPadId, 0x017C);
TPHD_ASSERT_OFFSET(dCamera_c, mIsWolf, 0x0190);
TPHD_ASSERT_OFFSET(dCamera_c, mCurMode, 0x01A4);
TPHD_ASSERT_OFFSET(dCamera_c, mMainStick, 0x01BC);
TPHD_ASSERT_OFFSET(dCamera_c, mCStick, 0x01D8);
TPHD_ASSERT_OFFSET(dCamera_c, mCStickYState, 0x01F4);
TPHD_ASSERT_OFFSET(dCamera_c, mCamTypeData, 0x0684);
TPHD_ASSERT_OFFSET(dCamera_c, mCurType, 0x068C);
TPHD_ASSERT_OFFSET(dCamera_c, mDefaultType, 0x0694);
TPHD_ASSERT_OFFSET(dCamera_c, mRoomMapTool, 0x07AC);
TPHD_ASSERT_OFFSET(dCamera_c, mTagCamTool, 0x0860);
TPHD_ASSERT_OFFSET(dCamera_c, mGear, 0x0940);
TPHD_ASSERT_OFFSET(dCamera_c, mCamStyleData, 0x0B38);
TPHD_ASSERT_OFFSET(dCamera_c, mCurStyle, 0x0B40);
TPHD_ASSERT_OFFSET(dCamera_c, mCurStyleIndex, 0x0B44);

#define dCamera_PROCESS_OFF_BODY          0x248u
#define dCamera_PROCESS_OFF_RENDER_EYE    0x0D8u
#define dCamera_PROCESS_OFF_RENDER_CENTER 0x0E4u

#define dCamera_CSTICK_WINDOW 0x28u

enum {
    dCam_MODE_FOLLOW    = 0,
    dCam_MODE_RESET     = 1,
    dCam_MODE_LOCK      = 2,
    dCam_MODE_SUBJECT   = 4,
    dCam_MODE_CHARGE    = 7,
    dCam_MODE_RIDE_LOCK = 8,
    dCam_MODE_EVENT     = 10,
};

#define dCam_ENGINE_COUNT        20
#define dCam_ENGINE_ENTRY_STRIDE 8u

typedef int (*dCamEngine_t)(dCamera_c* camera, int style);

inline volatile dCamEngine_t* dCam_getEngineSlot(int index)
{
    u32 addr = TPHD_DATA(TPHD_DATA_ADDR_camEngineTbl) +
               (u32)index * dCam_ENGINE_ENTRY_STRIDE + 4u;
    return (volatile dCamEngine_t*)addr;
}

typedef int (*dCamera_reset_t)(dCamera_c* camera);
#define dCamera_reset ((dCamera_reset_t)TPHD_ADDR_dCamera_reset)

inline void* dCam_getProcess(void)
{
    return dComIfGp_getPlay()->mCameraPtr;
}

inline dCamera_c* dCam_getBody(void)
{
    u8* process = (u8*)dCam_getProcess();
    return process ? (dCamera_c*)(process + dCamera_PROCESS_OFF_BODY) : nullptr;
}

inline int dCam_getMode(void)
{
    dCamera_c* cam = dCam_getBody();
    return cam ? cam->mCurMode : -1;
}

inline int dCam_getStyleIndex(void)
{
    dCamera_c* cam = dCam_getBody();
    return cam ? cam->mCurStyleIndex : -1;
}

inline int dCam_getAlgorithm(void)
{
    dCamera_c* cam = dCam_getBody();
    return cam ? dCam_getStyleAlgorithm(cam->mCurStyle) : -1;
}

inline dCamera_xform_c* dCam_getDesiredXform(void)
{
    dCamera_c* cam = dCam_getBody();
    return cam ? &cam->mViewCache : nullptr;
}

inline dCamera_xform_c* dCam_getCurrentXform(void)
{
    dCamera_c* cam = dCam_getBody();
    return cam ? &cam->mCurrent : nullptr;
}

inline bool dCam_snapXform(const cXyz* at, const cXyz* eye)
{
    u8* process = (u8*)dCam_getProcess();
    if (!process || !at || !eye)
        return false;

    dCamera_c* cam = (dCamera_c*)(process + dCamera_PROCESS_OFF_BODY);
    cam->mCurrent.mAt = *at;
    cam->mCurrent.mEye = *eye;

    dCamera_reset(cam);

    *(cXyz*)(process + dCamera_PROCESS_OFF_RENDER_EYE) = *eye;
    *(cXyz*)(process + dCamera_PROCESS_OFF_RENDER_CENTER) = *at;
    return true;
}

inline bool dCam_isZTargetEngaged(const dCamera_c* camera)
{
    if (!camera)
        return false;
    return (dComIfGp_getPlayerStatus0(camera->mPadId) &
            dComIfGp_STATUS0_ZTARGET) != 0;
}

inline cXyz* dCam_getLinkPos(void)
{
    return *(cXyz* volatile*)TPHD_DATA(TPHD_DATA_ADDR_linkPosPtr);
}

inline bool dCam_getMirror(void)
{
    return *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_mirrorFlag) == 1;
}
