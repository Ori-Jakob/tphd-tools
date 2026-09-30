#pragma once

#include "libtphd/types.h"
#include "libtphd/assert.h"

struct cSGlobe {
    f32 mRadius;
    s16 mInclination;
    s16 mAzimuth;
};

TPHD_ASSERT_OFFSET(cSGlobe, mInclination, 0x04);
TPHD_ASSERT_OFFSET(cSGlobe, mAzimuth, 0x06);
TPHD_ASSERT_SIZE(cSGlobe, 0x08);
