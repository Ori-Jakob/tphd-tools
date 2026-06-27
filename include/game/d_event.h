// game/d_event.h -- the global story/event flag array (dSv_event_c::mEvent).
//
// TP keeps story progression as a 256-byte bitfield in the save block. dusklight
// edits it as `dComIfGs_getSaveData()->mEvent`; in TPHD that's at info + 0x7F0
// (GCN-aligned, like every field past info+0x64 -- field_last_stay 0x64, mItem
// 0x9C, etc. all match GCN, so mEvent at GCN 0x7F0 follows). Each named flag (see
// event_flags_table.h) is one bit: mEvent[byteIndex] & mask. Big-endian host but
// these are single bytes, so no swaps.
//
// NOTE: this is a SEPARATE array from the per-item flags the inventory editor
// pokes (golden bugs @ info+0xE5, hidden skills @ info+0x219); those live in the
// item region. The event array here is the story/cutscene/quest progression set.
#pragma once

#include "game/types.h"
#include "game/d_com_inf_game.h"      // GAME_ADDR_gameInfo_info
#include "game/event_flags_table.h"   // kEventFlags[] / EventFlagEntry

#define GAME_ADDR_eventFlags (GAME_ADDR_gameInfo_info + 0x7F0u)   // mEvent[256] @ 0x10145b38

static inline volatile u8* dEvent_byte(u8 byteIndex)
{
    return (volatile u8*)(GAME_ADDR_eventFlags + byteIndex);
}
static inline bool dEvent_get(u8 byteIndex, u8 mask)
{
    return (*dEvent_byte(byteIndex) & mask) != 0;
}
static inline void dEvent_set(u8 byteIndex, u8 mask, bool on)
{
    volatile u8* p = dEvent_byte(byteIndex);
    if (on) *p |= mask; else *p &= (u8)~mask;
}

// ---- per-area scene flags (chests / switches / items / keys / dungeon items) -
// Each area's scene flags (which chests are opened, which switches are thrown,
// which field items were collected, small-key count, map/compass/boss-key) live
// in a dSv_memBit_c. TPHD's LIVE current-area memBit is at info+0xDF8
// (mDungeonItem @ info+0xE15 =
// 0x1014615D, user-verified via CE: bit0 Map / bit1 Compass / bit2 Boss Key /
// bit3 Boss Defeated / bit4 Heart Container; mDungeonItem is memBit+0x1D, so the
// struct base is 0xE15-0x1D = 0xDF8). The old GCN dSv_info_c offset 0x958 does
// NOT identify this block in TPHD: it begins an unrelated 0x168-byte HD field.
// Never copy a dSv_memBit_c to info+0x958.
typedef struct dSv_memBit_c {
    u32 mTbox[2];      // 0x00 treasure-chest opened bits (0..63)
    u32 mSwitch[4];    // 0x08 switch bits (0..127)
    u32 mItem[1];      // 0x18 field-item collected bits (0..31)
    u8  mKeyNum;       // 0x1C small keys
    u8  mDungeonItem;  // 0x1D map / compass / boss-key / boss / ... bits
} dSv_memBit_c;

static_assert(sizeof(dSv_memBit_c) == 0x20, "dSv_memBit_c layout changed");

#define GAME_ADDR_memBitTemp (GAME_ADDR_gameInfo_info + 0xDF8u)   // live current area 0x10146140
#define GAME_ADDR_memBitSave (GAME_ADDR_gameInfo_info + 0x2F0u)   // mSave[0].mBit; + region*0x20
#define DSV_MEMBIT_STRIDE    0x20u

#define g_areaTempBit ((volatile dSv_memBit_c*)GAME_ADDR_memBitTemp)

static inline volatile dSv_memBit_c* dArea_saveBit(int region)
{
    return (volatile dSv_memBit_c*)(GAME_ADDR_memBitSave + (u32)region * DSV_MEMBIT_STRIDE);
}

// ---- per-zone "stage event" scratch pool ------------------------------------
// A 32-slot array of 0x20-byte records at info+0xE54 (0x1014619C), distinct from
// the chest/switch memBit above. The save initializer (FUN_0290b914) builds it as
// {count=32, stride=0x20}; each slot's first byte is an id/marker (0xFF = empty)
// and a per-slot regen timer (FUN_02aba2f8) clears fields when it expires. The
// transient per-zone event state the map writes lands in the slot for that zone --
// editable live, but slot-indexed, so the active slot moves with the zone.
#define GAME_ADDR_stageEventPool (GAME_ADDR_gameInfo_info + 0xE54u)   // 0x1014619C
#define DSV_STAGE_EVENT_SLOTS    32
#define DSV_STAGE_EVENT_STRIDE   0x20u

static inline volatile u8* dStageEvent_slot(int slot)
{
    return (volatile u8*)(GAME_ADDR_stageEventPool + (u32)slot * DSV_STAGE_EVENT_STRIDE);
}

// mDungeonItem bit ids (dSv_memBit_c enum).
enum {
    DMEM_MAP = 0, DMEM_COMPASS, DMEM_BOSS_KEY, DMEM_BOSS_DEFEATED,
    DMEM_HEART_CONTAINER, DMEM_BOSS_DEMO, DMEM_OOCCOO, DMEM_MINIBOSS
};

static inline bool dMem_getBit(volatile u32* arr, int no)
{
    return (arr[no >> 5] >> (no & 31)) & 1u;
}
static inline void dMem_setBit(volatile u32* arr, int no, bool on)
{
    volatile u32* w = &arr[no >> 5];
    u32 m = 1u << (no & 31);
    if (on) *w |= m; else *w &= ~m;
}
