#pragma once

#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

inline bool fopScnRq_isLoading(void)
{
    return *(volatile u32*)TPHD_DATA(TPHD_DATA_ADDR_fopScnRq_isLoading) != 0;
}
