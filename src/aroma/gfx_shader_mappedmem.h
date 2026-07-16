#pragma once

#include <whb/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL TphdGfxLoadGFDShaderGroupMapped(WHBGfxShaderGroup* group,
                                     uint32_t index,
                                     const void* file);
BOOL TphdGfxInitFetchShaderMapped(WHBGfxShaderGroup* group);
BOOL TphdGfxFreeShaderGroupMapped(WHBGfxShaderGroup* group);

#ifdef __cplusplus
}
#endif
