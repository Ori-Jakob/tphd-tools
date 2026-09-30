#pragma once

#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

struct mDoRst_data_c {
    s32 mReset;
    s32 mResetPrepare;
    s32 m3ButtonReset;
    s32 m3ButtonResetPort;
    u8  mShutdown;
    u8  mReturnToMenu;
    u8  mLogoScnFlag;
    u8  mProgSeqFlag;
    u8  mProgChgFlag;
    u8  mWarningDispFlag;
    u8  field_0x16[2];
};

TPHD_ASSERT_SIZE(mDoRst_data_c, 0x18);

inline volatile mDoRst_data_c* mDoRst_getData(void)
{
    return *(volatile mDoRst_data_c* volatile*)
        TPHD_DATA(TPHD_DATA_ADDR_mDoRst_mResetData);
}

inline bool mDoRst_requestReset(void)
{
    volatile mDoRst_data_c* data = mDoRst_getData();
    if (!data)
        return false;
    data->mReset = 1;
    return true;
}
