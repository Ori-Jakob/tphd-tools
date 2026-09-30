#pragma once

#include "Z2AudioLib/Z2StatusMgr.h"
#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

#define Z2AUDIOMGR_OFF_STATUS 0x4E4u

enum {
    Z2SE_WARP_MAP_DECIDE = 0x48,
    Z2SE_SYS_ERROR       = 0x4A,
};

typedef int (*Z2AudioMgr_seStart_t)(void* mgr, u32* id, void* p2, int a, int b,
                                    int c, float f1, float f2, float f3, float f4);
#define Z2AudioMgr_seStart ((Z2AudioMgr_seStart_t)TPHD_ADDR_Z2AudioMgr_seStart)

inline void* Z2GetAudioMgr(void)
{
    return *(void* volatile*)TPHD_DATA(TPHD_DATA_ADDR_z2AudioMgrPtr);
}

inline volatile Z2StatusMgr* Z2GetStatusMgr(void)
{
    void* mgr = Z2GetAudioMgr();
    if (!mgr)
        return nullptr;
    return (volatile Z2StatusMgr*)((u8*)mgr + Z2AUDIOMGR_OFF_STATUS);
}

inline bool Z2GetGameClock(u8* hour, u8* minute)
{
    volatile Z2StatusMgr* status = Z2GetStatusMgr();
    if (!status)
        return false;
    *hour = status->mHour;
    *minute = status->mMinute;
    return true;
}

inline void Z2SeStart(u32 soundId)
{
    void* mgr = Z2GetAudioMgr();
    if (!mgr)
        return;
    u32 id = soundId;
    Z2AudioMgr_seStart(mgr, &id, nullptr, 0, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f);
}
