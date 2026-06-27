// game/d_pad.h -- the boot controller-select (mDoCPd control-pad manager).
//
// At boot, BEFORE the file-select screen, TPHD shows a "choose your controller"
// screen. Confirming a choice calls mDoCPd_controlPadMgr_setMode(mgr, mode), which
// builds the concrete Wii U input providers and makes one active:
//   mode 3 = Wii U GamePad (DRC, selectedIndex 0)
//   mode 6 = Pro Controller (selectedIndex 1)
// Until then the manager sits at its startup mode 5 (set by main(), providers not
// finalized). Forcing a save / debug-save load from the title screen in that state
// crashes, because the load runs gameplay against an input provider that was never
// selected. dPad_ensureControllerSelected() makes that selection ourselves -- the
// exact call the select screen makes -- so a load from the title screen behaves as
// if the player had already passed the controller-select screen.
//
// RE (Ghidra, TPHD Zelda.rpx US, module 0x1A03E108/0xA3175EEA):
//   FUN_02008fb8                          -> returns the control-pad mgr (g @ 0x100D3B94)
//   mDoCPd_controlPadMgr_setMode(mgr,mode) @ 0x02008990   (sets mgr+0x14 = mode)
//   FUN_02008fc4(mgr, kind)                @ 0x02008fc4   -> provider "is connected?"
//   FUN_029435ec (controller-select scene): select GamePad -> setMode(mgr, 3),
//                              if FUN_02008fc4(mgr,4) -> select Pro -> setMode(mgr, 6)
#pragma once

#include "game/types.h"
#include "logger.h"             // log which controller we finalize for a load

#define GAME_ADDR_controlPadMgrPtr 0x100D3B94u   // void* : the mDoCPd control-pad mgr

enum {
    PAD_MODE_GAMEPAD = 3,   // Wii U GamePad (DRC)
    PAD_MODE_PRO     = 6,   // Pro Controller
    PAD_MODE_STARTUP = 5,   // main()'s default, before the select screen runs
};

// "Connected?" probe kinds for dPad_isConnected (the kind args FUN_02008fc4 takes):
//   kind 3 -> GamePad/DRC provider (mgr+0x08),  kind 4 -> Pro provider (mgr+0x0c).
// Both slots are built at startup (mode 5), so both are valid to query before the
// player has chosen anything on the select screen.
enum {
    PAD_PROBE_GAMEPAD = 3,
    PAD_PROBE_PRO     = 4,
};

// Controller preference for our title-screen loads (matches ov::ControllerPref):
//   AUTO    -> Pro if one is connected (multiple controllers), else GamePad
//   GAMEPAD -> always the GamePad
//   PRO     -> Pro if connected, else fall back to the GamePad
enum {
    PAD_PREF_AUTO    = 0,
    PAD_PREF_GAMEPAD = 1,
    PAD_PREF_PRO     = 2,
};

typedef void (*mDoCPd_controlPadMgr_setMode_t)(void* mgr, u32 mode);
#define mDoCPd_controlPadMgr_setMode ((mDoCPd_controlPadMgr_setMode_t)0x02008990u)

// Returns nonzero if the provider of `kind` reports a device connected.
typedef u32 (*mDoCPd_controlPadMgr_isConnected_t)(void* mgr, u32 kind);
#define mDoCPd_controlPadMgr_isConnected ((mDoCPd_controlPadMgr_isConnected_t)0x02008fc4u)

static inline void* dPad_getControlPadMgr(void)
{
    return *(void* volatile*)GAME_ADDR_controlPadMgrPtr;
}

static inline u32 dPad_getControllerMode(void)
{
    void* mgr = dPad_getControlPadMgr();
    return mgr ? *(volatile u32*)((u8*)mgr + 0x14) : 0u;
}

// True once the player has chosen a controller on the boot select screen (the mgr
// is in a normal single-player gameplay mode, not the startup default).
static inline bool dPad_isControllerSelected(void)
{
    u32 m = dPad_getControllerMode();
    return m == (u32)PAD_MODE_GAMEPAD || m == (u32)PAD_MODE_PRO;
}

// Is a device of the given probe kind currently connected?
static inline bool dPad_isConnected(int probeKind)
{
    void* mgr = dPad_getControlPadMgr();
    return mgr && mDoCPd_controlPadMgr_isConnected(mgr, (u32)probeKind) != 0;
}

static inline bool dPad_isGamePadConnected(void) { return dPad_isConnected(PAD_PROBE_GAMEPAD); }
static inline bool dPad_isProConnected(void)     { return dPad_isConnected(PAD_PROBE_PRO); }

// Resolve a preference (PAD_PREF_*) to a concrete select mode given what's plugged
// in: AUTO and PRO both want the Pro when one is connected, else the GamePad.
static inline u32 dPad_chooseMode(int pref)
{
    if (pref == PAD_PREF_GAMEPAD)
        return PAD_MODE_GAMEPAD;
    return dPad_isProConnected() ? PAD_MODE_PRO : PAD_MODE_GAMEPAD;   // AUTO or PRO
}

// If no controller has been finalized yet (still at the boot default), select one
// now -- the same call the controller-select screen makes -- so a load from the
// title screen doesn't run gameplay against an unselected input provider. `pref`
// (PAD_PREF_*) picks which: AUTO prefers a connected Pro (i.e. when more than the
// GamePad is plugged in) and otherwise the GamePad. No-op once a controller is
// chosen, so it never overrides the player's controller mid-session.
static inline void dPad_ensureControllerSelected(int pref)
{
    void* mgr = dPad_getControlPadMgr();
    if (!mgr || dPad_isControllerSelected())
        return;   // no manager yet, or the player already chose -- leave it alone

    u32 mode = dPad_chooseMode(pref);
    mDoCPd_controlPadMgr_setMode(mgr, mode);
    Logger::Log("[tphd_tools] controller selected for load: %s (pref=%d)",
                mode == (u32)PAD_MODE_PRO ? "Pro Controller" : "GamePad", pref);
}

// Force-select a controller from a preference (PAD_PREF_*) right now, even if one
// was already chosen -- the Settings UI uses this so picking an option applies
// immediately (unlike dPad_ensureControllerSelected, which leaves an existing
// choice alone). AUTO/PRO resolve to the Pro when one is connected, else the
// GamePad, so this never selects a provider with no device behind it. No-op only
// when the pad manager isn't up yet (e.g. very early boot).
static inline void dPad_setController(int pref)
{
    void* mgr = dPad_getControlPadMgr();
    if (!mgr)
        return;
    u32 mode = dPad_chooseMode(pref);
    mDoCPd_controlPadMgr_setMode(mgr, mode);
    Logger::Log("[tphd_tools] controller set from settings: %s (pref=%d)",
                mode == (u32)PAD_MODE_PRO ? "Pro Controller" : "GamePad", pref);
}
