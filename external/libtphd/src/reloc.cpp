#include "libtphd/reloc.h"

extern "C" {
uint32_t g_tphdGameDataDelta = 0;

void TphdSetGameDataDelta(uint32_t delta)
{
    g_tphdGameDataDelta = delta;
}
}
