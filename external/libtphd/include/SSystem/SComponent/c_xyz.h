#pragma once

#include "libtphd/types.h"

struct Vec {
    f32 x, y, z;
};

struct cXyz : Vec {
    void set(f32 i_x, f32 i_y, f32 i_z) { x = i_x; y = i_y; z = i_z; }
    void zero(void) { x = y = z = 0.0f; }
};
