#pragma once

#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

inline bool mDoGph_isDrcOnly(void)
{
    return *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_gphDrcOnly) != 0;
}
