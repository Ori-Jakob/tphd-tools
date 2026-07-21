// game/c_damagereaction.h -- native encounter demo-skip state.
//
// cDmr_SkipInfo is the game's process-global handoff used by boss/miniboss demo
// skip callbacks across a same-room scene rebuild. TPHD US Zelda.rpx Ghidra:
//   Deku Toad demo_skip  FUN_0223062c writes 1 then changeScene(point 2)
//   Deku Toad create     FUN_02234638 consumes and clears it
#pragma once

#include "game/types.h"

#define GAME_ADDR_cDmr_SkipInfo 0x100D3BC8u

static inline u8 cDmr_getSkipInfo(void)
{
    return *(volatile u8*)GAME_ADDR_cDmr_SkipInfo;
}

static inline void cDmr_setSkipInfo(u8 value)
{
    *(volatile u8*)GAME_ADDR_cDmr_SkipInfo = value;
}

