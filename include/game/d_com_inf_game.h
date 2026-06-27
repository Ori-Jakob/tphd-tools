// game/d_com_inf_game.h -- access to the global game info / player.
//
// In the TP engine `dComIfGp_getPlayer()` returns g_dComIfG_gameInfo.play.mPlayer0
// (Link's actor). The RPL runs in the game's address space and Cemu loads
// Zelda.rpx at fixed addresses (matching Ghidra), so we read the global directly.
#pragma once

#include "game/f_op_actor.h"

// &g_dComIfG_gameInfo.play.mPlayer0 (TPHD Zelda.rpx, US v81).
// Ghidra label: g_dComIfG_gameInfo_play_mPlayer0 @ 0x1014b5b0.
#define GAME_ADDR_mPlayer0  0x1014b5b0

// g_dComIfG_gameInfo starts with `dSv_info_c info` (the whole persistent save
// block: player status, inventory, events, area flags, restart info, ...).
// Base verified at 0x10145348 (NOT the stale, xref-less Ghidra label at
// 0x101457e8): the field dSv_player_field_last_stay_info_c sits at 0x101453ac,
// which is GCN offset 0x64 into info -> info = 0x101453ac - 0x64 = 0x10145348.
// This is the same address gameplay reads/writes for live hearts/rupees
// (info.mPlayer.mStatusA, life @ +0x04), confirming info IS the live store.
// Size = play_base - info_base = 0x10146720 - 0x10145348 = 0x13D8 (play_base
// from mStartStage @ 0x1014a5e8 minus its 0x3EC8 offset within dComIfG_play_c).
// A full save-state snapshots/restores these bytes (see tools/save_state).
#define GAME_ADDR_gameInfo_info  0x10145348u
#define GAME_DSVINFO_SIZE        0x13D8u
#define GAME_ADDR_gameInfo_play  0x10146720u
// The typed player-status accessor lives in game/d_save.h (g_meterInfo->...).

// Small engine control block observed in live TPHD memory. The byte at
// 0x10296023 requests a full game reset when written to 1.
#define GAME_ADDR_gameControl 0x10296020u

typedef struct dComIfG_gameControl_c {
    u8 field_0x00[0x03];
    u8 gameReset;              // 0x03 -> 0x10296023
} dComIfG_gameControl_c;

static inline volatile dComIfG_gameControl_c* dComIfG_getGameControl(void)
{
    return (volatile dComIfG_gameControl_c*)GAME_ADDR_gameControl;
}

static inline void dComIfG_requestGameReset(void)
{
    dComIfG_getGameControl()->gameReset = 1;
}

// Pointer to Link's actor, or nullptr before Link has spawned
// (title screen, loading, ...). Mirrors dComIfGp_getPlayer(0).
static inline fopAc_ac_c* dComIfGp_getPlayer(void)
{
    return *(fopAc_ac_c* volatile*)GAME_ADDR_mPlayer0;
}
