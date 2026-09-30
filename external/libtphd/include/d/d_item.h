#pragma once

#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "libtphd/addr_v81.h"
#include "libtphd/types.h"

#define dItem_NONE 0xFFu

#define dItem_NORMAL_BOMB 0x70u
#define dItem_WATER_BOMB  0x71u
#define dItem_POKE_BOMB   0x72u

typedef u16 (*dSv_getRupeeMax_t)(const dSv_player_status_a_c* status);
#define dSv_getRupeeMax ((dSv_getRupeeMax_t)TPHD_ADDR_dSv_getRupeeMax)

typedef void (*dSv_player_item_c_setItem_t)(void* item, int slot, u8 itemNo);
#define dSv_player_item_c_setItem \
    ((dSv_player_item_c_setItem_t)TPHD_ADDR_dSv_player_item_c_setItem)

inline volatile dSv_player_item_c* dComIfGs_getItem(void)
{
    return (volatile dSv_player_item_c*)&dComIfGs_getSaveData()->mItem;
}

inline volatile dSv_player_get_item_c* dComIfGs_getGetItem(void)
{
    return (volatile dSv_player_get_item_c*)&dComIfGs_getSaveData()->mGetItem;
}

inline volatile dSv_player_item_record_c* dComIfGs_getItemRecord(void)
{
    return (volatile dSv_player_item_record_c*)&dComIfGs_getSaveData()->mItemRecord;
}

inline volatile dSv_player_item_max_c* dComIfGs_getItemMax(void)
{
    return (volatile dSv_player_item_max_c*)&dComIfGs_getSaveData()->mItemMax;
}

inline u8 dComIfGs_getItemSlot(int slot)
{
    return dComIfGs_getItem()->mItems[slot];
}

inline void dComIfGs_setItemSlot(int slot, u8 itemNo)
{
    dSv_player_item_c_setItem((void*)dComIfGs_getItem(), slot, itemNo);
}

inline bool dComIfGs_isItemFirstBit(u8 itemNo)
{
    return (dComIfGs_getGetItem()->mItemFlags[itemNo >> 5] >> (itemNo & 31)) & 1u;
}

inline void dComIfGs_setItemFirstBit(u8 itemNo, bool on)
{
    volatile u32* w = &dComIfGs_getGetItem()->mItemFlags[itemNo >> 5];
    u32 mask = 1u << (itemNo & 31);
    if (on)
        *w |= mask;
    else
        *w &= ~mask;
}

inline volatile u8* dComIfGs_saveByte(u32 off)
{
    return (volatile u8*)((u8*)dComIfGs_getSaveData() + off);
}

inline bool dComIfGs_getSaveBitAt(u32 off, int bit)
{
    return (*dComIfGs_saveByte(off) >> bit) & 1u;
}

inline void dComIfGs_setSaveBitAt(u32 off, int bit, bool on)
{
    volatile u8* p = dComIfGs_saveByte(off);
    u8 m = (u8)(1u << bit);
    if (on)
        *p |= m;
    else
        *p &= (u8)~m;
}
