#pragma once

#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

#define mDoCPd_MGR_OFF_MODE 0x14u

enum {
    mDoCPd_MODE_GAMEPAD = 3,
    mDoCPd_MODE_PRO     = 6,
    mDoCPd_MODE_STARTUP = 5,
};

enum {
    mDoCPd_PROBE_GAMEPAD = 3,
    mDoCPd_PROBE_PRO     = 4,
};

enum {
    mDoCPd_PREF_AUTO    = 0,
    mDoCPd_PREF_GAMEPAD = 1,
    mDoCPd_PREF_PRO     = 2,
};

typedef void (*mDoCPd_controlPadMgr_setMode_t)(void* mgr, u32 mode);
#define mDoCPd_controlPadMgr_setMode \
    ((mDoCPd_controlPadMgr_setMode_t)TPHD_ADDR_mDoCPd_controlPadMgr_setMode)

typedef u32 (*mDoCPd_controlPadMgr_isConnected_t)(void* mgr, u32 kind);
#define mDoCPd_controlPadMgr_isConnected \
    ((mDoCPd_controlPadMgr_isConnected_t)TPHD_ADDR_mDoCPd_controlPadMgr_isConnected)

inline void* mDoCPd_getControlPadMgr(void)
{
    return *(void* volatile*)TPHD_DATA(TPHD_DATA_ADDR_controlPadMgrPtr);
}

inline u32 mDoCPd_getMode(void)
{
    void* mgr = mDoCPd_getControlPadMgr();
    return mgr ? *(volatile u32*)((u8*)mgr + mDoCPd_MGR_OFF_MODE) : 0u;
}

inline bool mDoCPd_isControllerSelected(void)
{
    u32 m = mDoCPd_getMode();
    return m == (u32)mDoCPd_MODE_GAMEPAD || m == (u32)mDoCPd_MODE_PRO;
}

inline bool mDoCPd_isConnected(int probeKind)
{
    void* mgr = mDoCPd_getControlPadMgr();
    return mgr && mDoCPd_controlPadMgr_isConnected(mgr, (u32)probeKind) != 0;
}

inline bool mDoCPd_isProConnected(void)
{
    return mDoCPd_isConnected(mDoCPd_PROBE_PRO);
}

inline u32 mDoCPd_chooseMode(int pref)
{
    if (pref == mDoCPd_PREF_GAMEPAD)
        return mDoCPd_MODE_GAMEPAD;
    return mDoCPd_isProConnected() ? mDoCPd_MODE_PRO : mDoCPd_MODE_GAMEPAD;
}

inline u32 mDoCPd_ensureControllerSelected(int pref)
{
    void* mgr = mDoCPd_getControlPadMgr();
    if (!mgr || mDoCPd_isControllerSelected())
        return 0;
    u32 mode = mDoCPd_chooseMode(pref);
    mDoCPd_controlPadMgr_setMode(mgr, mode);
    return mode;
}

inline u32 mDoCPd_setController(int pref)
{
    void* mgr = mDoCPd_getControlPadMgr();
    if (!mgr)
        return 0;
    u32 mode = mDoCPd_chooseMode(pref);
    mDoCPd_controlPadMgr_setMode(mgr, mode);
    return mode;
}

struct mDoCPd_input_c {
    u32   mButtons;
    float mLeftX, mLeftY, mRightX, mRightY;
    u8    mType;
};

enum {
    mDoCPd_BTN_A  = 0x00000010u,
    mDoCPd_BTN_B  = 0x00000040u,
    mDoCPd_BTN_X  = 0x00000008u,
    mDoCPd_BTN_Y  = 0x00000020u,
    mDoCPd_BTN_ZR = 0x00000004u,
    mDoCPd_BTN_ZL = 0x00000080u,
    mDoCPd_BTN_R  = 0x00000200u,
    mDoCPd_BTN_L  = 0x00002000u,
    mDoCPd_BTN_R3 = 0x00010000u,
    mDoCPd_BTN_L3 = 0x00020000u,
};

inline bool mDoCPd_getInput(mDoCPd_input_c* o)
{
    u32 ctrl = *(volatile u32*)TPHD_DATA(TPHD_DATA_ADDR_controller);
    if (!ctrl)
        return false;
    u32 p = *(volatile u32*)(ctrl + 0x04);
    if (!p)
        return false;

    u8 type = *(volatile u8*)TPHD_DATA(TPHD_DATA_ADDR_controllerType);
    u32 base;
    if (type == 1) {
        u32 q = *(volatile u32*)(p + 0x10);
        if (!q)
            return false;
        base = q + 0x70;
    } else {
        base = p + 0x0C;
    }

    o->mButtons = *(volatile u32*)(base + 0x00);
    o->mLeftX   = *(volatile float*)(base + 0x0C);
    o->mLeftY   = *(volatile float*)(base + 0x10);
    o->mRightX  = *(volatile float*)(base + 0x14);
    o->mRightY  = *(volatile float*)(base + 0x18);
    o->mType    = type;
    return true;
}

inline u32 mDoCPd_normalizeButtons(u32 g, u8 type)
{
    if (type == 1)
        return g;
    u32 p = 0;
    if (g & 0x00008000) p |= 0x00000010;
    if (g & 0x00004000) p |= 0x00000040;
    if (g & 0x00002000) p |= 0x00000008;
    if (g & 0x00001000) p |= 0x00000020;
    if (g & 0x00000008) p |= 0x00000400;
    if (g & 0x00000004) p |= 0x00001000;
    if (g & 0x00040000) p |= 0x00020000;
    if (g & 0x00020000) p |= 0x00010000;
    if (g & 0x00000020) p |= 0x00002000;
    if (g & 0x00000080) p |= 0x00000080;
    if (g & 0x00000010) p |= 0x00000200;
    if (g & 0x00000040) p |= 0x00000004;
    if (g & 0x00000200) p |= 0x00000001;
    if (g & 0x00000100) p |= 0x00004000;
    if (g & 0x00000800) p |= 0x00000002;
    if (g & 0x00000400) p |= 0x00008000;
    return p;
}
