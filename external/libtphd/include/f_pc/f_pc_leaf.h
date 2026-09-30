#pragma once

#include "f_pc/f_pc_base.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct leafdraw_class : base_process_class {
    void* leaf_methods;
    s8    field_0xbc;
    u8    field_0xbd;
    u16   draw_priority;
};

TPHD_ASSERT_SIZE(leafdraw_class, 0xC0);
