#pragma once

#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct dEvt_control_c {
    u8 field_0x000[0xE5];
    u8 mEventStatus;
    u8 field_0x0e6[0x12A];
};

TPHD_ASSERT_OFFSET(dEvt_control_c, mEventStatus, 0xE5);

inline dEvt_control_c* dComIfGp_getEvent(void)
{
    return (dEvt_control_c*)dComIfGp_getPlay()->mEvent;
}

inline bool dEvt_isEventRunning(void)
{
    return dComIfGp_getEvent()->mEventStatus != 0;
}

inline void dEvt_setEventRunning(bool on)
{
    dComIfGp_getEvent()->mEventStatus = on ? 1 : 0;
}

inline bool dEvent_isCameraPlay(void)
{
    return dComIfGp_getPlay()->mCameraPlay != 0;
}

inline void dEvent_setCameraPlay(bool on)
{
    dComIfGp_getPlay()->mCameraPlay = on ? 1 : 0;
}

inline volatile u8* dComIfGs_eventByte(u8 byteIndex)
{
    return (volatile u8*)&dComIfGs_getSaveData()->mEvent.mEvent[byteIndex];
}

inline bool dComIfGs_isEventBit(u8 byteIndex, u8 mask)
{
    return (*dComIfGs_eventByte(byteIndex) & mask) != 0;
}

inline void dComIfGs_setEventBit(u8 byteIndex, u8 mask, bool on)
{
    volatile u8* p = dComIfGs_eventByte(byteIndex);
    if (on)
        *p |= mask;
    else
        *p &= (u8)~mask;
}

#define dEvent_F0800_BYTE 0x61u
#define dEvent_F0800_MASK 0x40u

inline volatile dSv_memBit_c* dComIfGs_getTempBit(void)
{
    return (volatile dSv_memBit_c*)&dComIfGs_getSaveData()->mMemory;
}

inline volatile dSv_memBit_c* dComIfGs_getSaveBit(int region)
{
    if (region < 0 || region >= dSv_SAVE_COUNT)
        return nullptr;
    return (volatile dSv_memBit_c*)&dComIfGs_getSaveData()->mSave[region];
}

inline bool dComIfGs_isBit(volatile u32* arr, int no)
{
    return (arr[no >> 5] >> (no & 31)) & 1u;
}

inline void dComIfGs_setBit(volatile u32* arr, int no, bool on)
{
    volatile u32* w = &arr[no >> 5];
    u32 m = 1u << (no & 31);
    if (on)
        *w |= m;
    else
        *w &= ~m;
}

inline volatile dSv_zone_c* dComIfGs_getZone(int slot)
{
    if (slot < 0 || slot >= dSv_ZONE_COUNT)
        return nullptr;
    return (volatile dSv_zone_c*)&dComIfGs_getSaveData()->mZone[slot];
}
