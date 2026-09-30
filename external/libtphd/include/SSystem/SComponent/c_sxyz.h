#pragma once

#include "libtphd/types.h"

struct SVec {
    s16 x, y, z;
};

struct csXyz : SVec {
    void set(s16 i_x, s16 i_y, s16 i_z) { x = i_x; y = i_y; z = i_z; }
};
