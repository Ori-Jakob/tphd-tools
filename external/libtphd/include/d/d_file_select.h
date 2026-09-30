#pragma once

#include "d/d_procname.h"
#include "f_op/f_op_actor_mng.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

enum {
    dFile_select_PATH_NEW_FILE = 4,
};

enum {
    dFile_select_STATE_NEW_FILE_COMMITTED = 0x39,
};

enum {
    dFile_select_RESULT_PENDING = 0,
    dFile_select_RESULT_SUCCESS = 1,
    dFile_select_RESULT_FAILURE = 2,
};

struct dFile_select_c {
    u8 field_0x000[0x2B8];
    u8 mSlotState[3];
    u8 mSlotError[3];
    u8 field_0x2be[0x2C6 - 0x2BE];
    u8 mSelectedSlot;
    u8 field_0x2c7[0x2CA - 0x2C7];
    u8 mHeroModeChoice;
    u8 mPreviousHeroChoice;
    u8 field_0x2cc[0x2D1 - 0x2CC];
    u8 mSelectionPath;
    u8 mState;
    u8 mLoadResult;
};

TPHD_ASSERT_OFFSET(dFile_select_c, mSelectedSlot, 0x2C6);
TPHD_ASSERT_OFFSET(dFile_select_c, mHeroModeChoice, 0x2CA);
TPHD_ASSERT_OFFSET(dFile_select_c, mSelectionPath, 0x2D1);
TPHD_ASSERT_OFFSET(dFile_select_c, mState, 0x2D2);
TPHD_ASSERT_OFFSET(dFile_select_c, mLoadResult, 0x2D3);

struct dScnName_c {
    u8 field_0x000[0x414];
    dFile_select_c* mFileSelect;
};

TPHD_ASSERT_OFFSET(dScnName_c, mFileSelect, 0x414);

inline dScnName_c* dScnName_get(void)
{
    return (dScnName_c*)fpcM_searchProcessByName(fpcNm_SCENE_NAME);
}

inline dFile_select_c* dScnName_getFileSelect(void)
{
    dScnName_c* scene = dScnName_get();
    return scene ? scene->mFileSelect : nullptr;
}
