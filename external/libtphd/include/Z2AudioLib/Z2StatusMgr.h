#pragma once

#include "libtphd/assert.h"
#include "libtphd/types.h"

struct Z2StatusMgr {
    u8  mHour;
    u8  mMinute;
    u8  mWeekday;
    u8  field_0x03;
    u16 mTime;
};

TPHD_ASSERT_OFFSET(Z2StatusMgr, mTime, 0x04);
