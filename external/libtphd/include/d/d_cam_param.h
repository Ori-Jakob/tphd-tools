#pragma once

#include "libtphd/assert.h"
#include "libtphd/types.h"

#define dCam_TYPE_MODE_COUNT 11

struct dCamType_c {
    char mName[24];
    s16  mStyle[2][dCam_TYPE_MODE_COUNT];
};

TPHD_ASSERT_OFFSET(dCamType_c, mStyle, 0x18);
TPHD_ASSERT_SIZE(dCamType_c, 0x44);

struct dCamParam_c {
    u8  field_0x00[4];
    u16 mAlgorithm;
    u16 mFlags;
    u8  field_0x08[0x78 - 0x08];
};

TPHD_ASSERT_OFFSET(dCamParam_c, mAlgorithm, 0x04);
TPHD_ASSERT_SIZE(dCamParam_c, 0x78);

struct dCamMapToolData_c {
    u8  field_0x00[0x30];
    int mCameraIndex;
    u8  field_0x34[8];
};

TPHD_ASSERT_OFFSET(dCamMapToolData_c, mCameraIndex, 0x30);
TPHD_ASSERT_SIZE(dCamMapToolData_c, 0x3C);

#define dCam_MAPTOOL_NONE 0xFF

enum {
    dCam_ALG_LET = 0,
    dCam_ALG_CHASE,
    dCam_ALG_LOCKON,
    dCam_ALG_TALKTO,
    dCam_ALG_SUBJECT,
    dCam_ALG_FIXED_POS,
    dCam_ALG_FIXED_FRAME,
    dCam_ALG_TOWER,
    dCam_ALG_RIDE,
    dCam_ALG_MANUAL,
    dCam_ALG_EVENT,
    dCam_ALG_HOOKSHOT,
    dCam_ALG_RAIL = 15,
};

inline int dCam_getStyleAlgorithm(const dCamParam_c* style)
{
    return style ? (int)*(const volatile u16*)&style->mAlgorithm : -1;
}
