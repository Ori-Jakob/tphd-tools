// game/types.h -- basic types for the TPHD game-bindings lib.
//
// A tiny, hand-rolled equivalent of tpgz's libtp_c: just enough of the game's
// types/structs/accessors (at offsets verified in TPHD's Zelda.rpx) to read game
// state directly from the injected RPL, e.g. `dComIfGp_getPlayer()->current.pos`.
#pragma once

#include <stdint.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef float    f32;

// Vec of floats (cXyz / Vec) and shorts (csXyz), as in the TP decomp.
typedef struct cXyz  { f32 x, y, z; } cXyz;    // 0x0C
typedef struct csXyz { s16 x, y, z; } csXyz;   // 0x06
