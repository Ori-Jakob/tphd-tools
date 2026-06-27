// game/z2_audio.h -- tiny Z2 audio/status accessors used by debug HUDs.
#pragma once

#include "game/types.h"

// Z2AudioMgr::mAudioMgrPtr / singleton pointer (TPHD Zelda.rpx, US v81).
#define GAME_ADDR_z2AudioMgrPtr 0x1028ee90u

// TPHD Zelda.rpx: Z2AudioMgr::gframeProcess passes (this + 0x4E4) to
// Z2StatusMgr::processTime (0x02C30654), which packs mHour:mMinute into mTime.
#define Z2AUDIOMGR_OFF_STATUS 0x4E4u

typedef struct Z2StatusMgr_tphd {
    u8  mHour;             // 0x00
    u8  mMinute;           // 0x01
    u8  mWeekday;          // 0x02
    u8  field_0x03;        // 0x03
    u16 mTime;             // 0x04 (hour * 256 + minute)
} Z2StatusMgr_tphd;

static inline volatile Z2StatusMgr_tphd* Z2GetStatusMgr_tphd(void)
{
    void* mgr = *(void* volatile*)GAME_ADDR_z2AudioMgrPtr;
    if (!mgr)
        return 0;
    return (volatile Z2StatusMgr_tphd*)((u8*)mgr + Z2AUDIOMGR_OFF_STATUS);
}

static inline bool Z2GetGameClock(u8* hour, u8* minute)
{
    volatile Z2StatusMgr_tphd* status = Z2GetStatusMgr_tphd();
    if (!status)
        return false;
    *hour = status->mHour;
    *minute = status->mMinute;
    return true;
}
