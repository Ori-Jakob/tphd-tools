// cemu/main.cpp -- the TPHD Tools RPL entry point (Cemu codecave front end).
//
// Exposes a single export, TPHDToolsEntry(reason, a, b), that the graphics-pack
// codecave routes every hook into. This is the Cemu-specific front end: it owns
// only the export + dispatch and hands the work to the shared overlay body
// (Overlay::Present) and input filters (Input::Filter*). The Aroma WUPS plugin
// (src/aroma/plugin.cpp) drives the exact same shared code from runtime function
// hooks instead -- see README "Aroma".
//
//   reason 0 (PRESENT): a = &g_abTvColorBuffer
//   reason 1 (VPAD):    a = VPADStatus* buffers, b = (int)sample count
//   reason 2 (KPAD):    a = KPADStatus* buffers, b = (int)sample count
//   reason 3 (DRC):     a = GX2ColorBuffer* immediately before DRC scan-out
//   reason 4 (PHASE_1): a = dScnPly* immediately before dScnPly::phase_1

#include <stdint.h>
#include <coreinit/debug.h>     // OSReport
#include <coreinit/dynload.h>   // OSDynLoad_Module, OSDynLoad_EntryReason
#include <gx2/surface.h>        // GX2ColorBuffer

#include "overlay.h"
#include "input.h"
#include "logger.h"
#include "tools/save_state.h"

static int Present(void* tvColorBuffer, void* drcColorBuffer)
{
    // Start the log on the first present, NOT in rpl_entry: StartNewLog spawns the
    // logger worker thread, and doing that from the RPL's entry-reason callback
    // runs while OSDynLoad still holds the loader lock (the codecave calls us via
    // OSDynLoad_Acquire). On Cemu that hangs the Acquire, the export never
    // resolves, and the overlay never draws. Present runs on the game's present
    // thread, well after the load completes, so it's safe here.
    static bool s_logStarted = false;
    if (!s_logStarted) {
        s_logStarted = true;
        Logger::StartNewLog();
        Logger::Log("[tphd_tools.rpl] first present -- RPL is live in the game process!");
    }

    (void)drcColorBuffer;
    Overlay::Present((GX2ColorBuffer*)tvColorBuffer, nullptr);
    return 0;
}

extern "C" {

// RPL module entry. Keep this trivial: it runs inside the dynamic loader (with the
// loader lock held), so no thread creation, FS, or logging here -- see Present().
int rpl_entry(OSDynLoad_Module module, OSDynLoad_EntryReason reason)
{
    (void)module;
    (void)reason;
    return 0;
}

// The ONLY exported function. Every codecave hook routes here with a selector.
int TPHDToolsEntry(int reason, void* a, void* b)
{
    switch (reason) {
    case 0:
        return Present(a, b);
    case 1:
        Input::FilterVpad(a, (int)(intptr_t)b);
        return 0;
    case 2:
        Input::FilterKpad(a, (int)(intptr_t)b);
        return 0;
    case 3:
        Overlay::PresentGamePad((GX2ColorBuffer*)a);
        return 0;
    case 4:
        Tools::SaveState::OnScenePhase1();
        return 0;
    default:
        return 0;
    }
}

} // extern "C"
