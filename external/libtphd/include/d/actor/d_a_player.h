#pragma once

#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"
#include "f_op/f_op_actor.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct daPy_demo_c {
    u8 field_0x00[4];
    u8 mDemoMode;
    u8 field_0x05[3];
};

struct daPy_py_c : fopAc_ac_c {
    u8   mCutType;
    u8   mComboCutCount;
    u8   mSpecialMode;
    u8   field_0x56b;
    s16  mDamageTimer;
    u16  mSwordUpTimer;
    u32  mNoResetFlg0;
    u32  mNoResetFlg1;
    u32  mNoResetFlg2;
    u32  mNoResetFlg3;
    u32  mResetFlg0;
    u32  mResetFlg1;
    u32  mEndResetFlg0;
    u32  mEndResetFlg1;
    u32  mEndResetFlg2;
    f32  field_0x594;
    f32  field_0x598;
    csXyz mBodyAngle;
    u8   field_0x5a2[2];
    cXyz mHeadTopPos;
    cXyz mItemPos;
    cXyz mSwordTopPos;
    cXyz mLeftHandPos;
    cXyz mRightHandPos;
    cXyz mLeftFootPos;
    cXyz mRightFootPos;
    cXyz field_0x5f8;
    daPy_demo_c mDemo;
};

TPHD_ASSERT_OFFSET(daPy_py_c, mNoResetFlg1, 0x574);
TPHD_ASSERT_OFFSET(daPy_py_c, mNoResetFlg3, 0x57C);
TPHD_ASSERT_OFFSET(daPy_py_c, mBodyAngle, 0x59C);
TPHD_ASSERT_OFFSET(daPy_py_c, mDemo, 0x604);

enum {
    daPy_STATUS_MIDNA      = 0x00000004u,
    daPy_STATUS_MAGNE      = 0x00001000u,
    daPy_STATUS_UNK800000  = 0x00800000u,
};

enum {
    daPy_STATUS3_BLOCK = 0x40000000u,
};

inline s16 daPy_getLookAngleY(const daPy_py_c* py) { return py->mBodyAngle.y; }
inline u8 daPy_getDemoMode(const daPy_py_c* py) { return py->mDemo.mDemoMode; }
