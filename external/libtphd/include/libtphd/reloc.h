#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t g_tphdGameDataDelta;

void TphdSetGameDataDelta(uint32_t delta);

#ifdef __cplusplus
}
#endif

#define TPHD_DATA(linkAddress) ((linkAddress) + g_tphdGameDataDelta)
