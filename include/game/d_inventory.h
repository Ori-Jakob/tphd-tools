// game/d_inventory.h -- typed access to the live save block for the inventory editor.
//
// Everything lives in the persistent save block info @ 0x10145348 (the same
// block the save-state/debug-save features snapshot). We're on a big-endian PPC
// host, so the save's BE u16/u32 values are read/written natively -- no swaps.
//
// Rather than one giant nested struct (the layout between status and the item
// arrays is TPHD-specific), each sub-struct is anchored at its verified absolute
// offset. Two regimes (see project memory):
//   * dSv_player_status_a_c is the GCN layout shifted +2 in TPHD
//     (maxLife @ info+0x02, wallet @ info+0x1B -- both confirmed live).
//   * mItem onward matches GCN exactly (mItems @ info+0x9C = 0x101453E4,
//     confirmed in the deserialize FUN_02aa8af8).
#pragma once

#include "game/types.h"
#include "game/d_com_inf_game.h"   // GAME_ADDR_gameInfo_info (0x10145348)
#include "game/item_table.h"

#define DSV_INFO       GAME_ADDR_gameInfo_info   // 0x10145348
#define DSV_ITEM_NONE  0xFFu

// ---- player status A (GCN layout + 2 in TPHD), anchored at info+0x00 ---------
typedef struct dSv_status_a_c {
    /* 0x00 */ u8  field_0x00[2];
    /* 0x02 */ u16 mMaxLife;
    /* 0x04 */ u16 mLife;
    /* 0x06 */ u16 mRupee;
    /* 0x08 */ u16 mMaxOil;
    /* 0x0A */ u16 mOil;
    /* 0x0C */ u8  field_0x0c;
    /* 0x0D */ u8  mSelectItem[4];   // equipped X / Y (slot indices, 0xFF = none)
    /* 0x11 */ u8  mMixItem[4];      // combo X / Y
    /* 0x15 */ u8  mSelectEquip[6];  // clothes, sword, shield, scent, ...
    /* 0x1B */ u8  mWalletSize;      // 0 normal, 1 big, 2 giant, 3 colossal
    /* 0x1C */ u8  mMaxMagic;
    /* 0x1D */ u8  mMagic;
    /* 0x1E */ u8  mMagicFlag;
    /* 0x1F */ u8  field_0x1f;
    /* 0x20 */ u8  mTransformStatus; // 0 human, 1 wolf
} dSv_status_a_c;

// ---- item structures (GCN offsets, identical in TPHD) -----------------------
typedef struct dSv_item_c {
    /* 0x00 */ u8 mItems[24];      // item wheel (item ids; 0xFF = empty)
    /* 0x18 */ u8 mItemSlots[24];  // lineup / display order
} dSv_item_c;

typedef struct dSv_getItem_c {
    /* 0x00 */ u32 mItemFlags[8];  // "have ever gotten" bit per item id
} dSv_getItem_c;

typedef struct dSv_itemRecord_c {
    /* 0x00 */ u8 mArrowNum;
    /* 0x01 */ u8 mBombNum[3];     // [bag0..2]
    /* 0x04 */ u8 mBottleNum[4];   // [bottle0..3]
    /* 0x08 */ u8 mPachinkoNum;    // slingshot seeds
} dSv_itemRecord_c;

typedef struct dSv_itemMax_c {
    /* 0x00 */ u8 mItemMax[8];     // [0]=arrows [1]=bombs [2]=water bombs [6]=bomblings
} dSv_itemMax_c;

// mItemMax[] indices.
#define DSV_MAX_ARROW        0
#define DSV_MAX_NORMAL_BOMB  1
#define DSV_MAX_WATER_BOMB   2
#define DSV_MAX_POKE_BOMB    6

// Typed views onto the live save block.
#define g_svStatusA    ((volatile dSv_status_a_c*) (DSV_INFO + 0x00))
#define g_svItem       ((volatile dSv_item_c*)     (DSV_INFO + 0x9C))
#define g_svGetItem    ((volatile dSv_getItem_c*)  (DSV_INFO + 0xCC))
#define g_svItemRecord ((volatile dSv_itemRecord_c*)(DSV_INFO + 0xEC))
#define g_svItemMax    ((volatile dSv_itemMax_c*)  (DSV_INFO + 0xF8))

// ---- item wheel get/set ------------------------------------------------------
static inline u8 dInv_getItem(int slot) { return g_svItem->mItems[slot]; }

// Set via the game so the lineup + equipped slots refresh.
// dSv_player_item_c::setItem(this, slot, itemNo) @ 0x02aa5b8c.
typedef void (*dSv_setItem_t)(void* itemStruct, int slot, u8 itemNo);
#define dSv_setItem ((dSv_setItem_t)0x02aa5b8cu)
static inline void dInv_setItem(int slot, u8 itemNo)
{
    dSv_setItem((void*)g_svItem, slot, itemNo);
}

// ---- got-item "first bit" flags: mItemFlags[id/32] & (1 << (id%32)) ----------
static inline bool dInv_getItemFlag(u8 itemNo)
{
    return (g_svGetItem->mItemFlags[itemNo >> 5] >> (itemNo & 31)) & 1u;
}
static inline void dInv_setItemFlag(u8 itemNo, bool on)
{
    volatile u32* w = &g_svGetItem->mItemFlags[itemNo >> 5];
    u32 mask = 1u << (itemNo & 31);
    if (on) *w |= mask; else *w &= ~mask;
}

// ---- equipment / collectibles (verified offsets from the CE table) ----------
// Equipped gear lives in statusA.mSelectEquip[0..3].
#define DSV_EQUIP_ARMOR  0
#define DSV_EQUIP_SWORD  1
#define DSV_EQUIP_SHIELD 2
#define DSV_EQUIP_SCENT  3

// Single-byte collectibles.
#define g_svFusedShadow ((volatile u8*)(DSV_INFO + 0x109u))  // bits 0-3 forest/fire/water/twilight
#define g_svMirror      ((volatile u8*)(DSV_INFO + 0x10Au))  // bits 0-3 desert/snowpeak/time/sky
#define g_svPoeSouls    ((volatile u8*)(DSV_INFO + 0x10Cu))  // count (max 60)

// Generic scattered-bit access (have-flags, golden bugs, hidden skills).
static inline volatile u8* dInv_byte(u32 off) { return (volatile u8*)(DSV_INFO + off); }
static inline bool dInv_getBit(u32 off, int bit) { return (*dInv_byte(off) >> bit) & 1u; }
static inline void dInv_setBit(u32 off, int bit, bool on)
{
    volatile u8* p = dInv_byte(off);
    u8 m = (u8)(1u << bit);
    if (on) *p |= m; else *p &= (u8)~m;
}
// have-gear bit fields (offset, bit):
#define DSV_OFF_HAVE_ARMOR   0xD1u   // b0 Magic Armor, b1 Zora Armor, b7 Hero's Clothes
#define DSV_OFF_HAVE_SWDSHLD 0xD2u   // b0 Ordon Sword, b1 Master, b2 Ordon Shield, b3 Wood Shield, b4 Hylian
#define DSV_OFF_HAVE_MSLIGHT 0xD6u   // b1 Master Sword (imbued with light)
// golden bugs: 3 bytes from 0xE5, 2 bits per species (male even, female odd).
#define DSV_OFF_BUGS         0xE5u
// hidden skills: 2 bytes from 0x219.
#define DSV_OFF_SKILL_LO     0x219u
#define DSV_OFF_SKILL_HI     0x21Au
