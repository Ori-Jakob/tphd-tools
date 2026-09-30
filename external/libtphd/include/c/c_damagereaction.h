#pragma once

#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

inline u8 cDmr_getSkipInfo(void)
{
    return *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_cDmr_SkipInfo);
}

inline void cDmr_setSkipInfo(u8 value)
{
    *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_cDmr_SkipInfo) = value;
}
