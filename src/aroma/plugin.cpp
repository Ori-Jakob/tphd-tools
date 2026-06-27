// aroma/plugin.cpp -- the TPHD Tools front end for Aroma (WiiU Plugin System).
//
// Same overlay, different delivery. On Cemu we're an RPL the graphics-pack
// codecave acquires and calls through one export (src/cemu/main.cpp). On real
// hardware under Aroma we're a WUPS plugin (.wps) that installs its own
// function-replacement hooks at runtime -- the tpgz-style "patch the engine's
// own functions" approach -- with no codecave and no game-side patch file.
//
// We replace three system functions and route them into the SAME shared code the
// Cemu front end uses:
//   GX2CopyColorBufferToScanBuffer -> Overlay::Present(TV) / PresentGamePad(DRC)
//   VPADRead                       -> Input::FilterVpad   (GamePad)
//   KPADReadEx                     -> Input::FilterKpad   (Pro/Classic/Wiimote)
//
// Why GX2CopyColorBufferToScanBuffer and not GX2SwapScanBuffers: swap takes no
// arguments -- by then there's no surface to draw into. The copy-to-scan-buffer
// call is the last point that still hands us the finished TV GX2ColorBuffer, so
// we draw ImGui into it *before* the real copy carries it to scan-out. This is
// exactly the buffer the Cemu codecave handed us as g_abTvColorBuffer.
//
// WUPS replacement declarations are static metadata consumed before
// ON_APPLICATION_START, so they cannot be conditionally installed by title ID.
// Scope them to game processes (never Wii U Menu), then make every wrapper a
// transparent call-through unless the running title is Twilight Princess HD.
// Game-function calls in the shared tools/cheats use absolute Zelda.rpx
// addresses, which are the same on console as in Cemu (the title's .text/.data
// load at fixed addresses), so no rebasing is needed.

#include <wups.h>

#include <coreinit/title.h>     // OSGetTitleID
#include <coreinit/debug.h>     // OSReport
#include <coreinit/cache.h>     // OSMemoryBarrier
#include <gx2/swap.h>           // GX2CopyColorBufferToScanBuffer
#include <gx2/enum.h>           // GX2ScanTarget, GX2_SCAN_TARGET_TV
#include <gx2/surface.h>        // GX2ColorBuffer
#include <vpad/input.h>         // VPADRead, VPADStatus, VPADReadError
#include <padscore/kpad.h>      // KPADReadEx, KPADStatus, KPADError

#include <stdarg.h>
#include <stdio.h>

#include "overlay.h"
#include "input.h"
#include "logger.h"
#include "version.h"


// WUPS plugin metadata (shown in the Aroma plugin config menu).
WUPS_PLUGIN_NAME("TPHD Tools");
WUPS_PLUGIN_DESCRIPTION("A collection of tools for Twilight Princess HD with an imGUI interface.");
WUPS_PLUGIN_VERSION(TPHD_TOOLS_VERSION);
WUPS_PLUGIN_AUTHOR(TPHD_TOOLS_AUTHOR);
WUPS_PLUGIN_LICENSE("");

WUPS_USE_WUT_DEVOPTAB();   // stdio paths for SD-card config/save-state files


// Title gate: only run inside Twilight Princess HD.
static const uint64_t kTitleIdUS = 0x000500001019E500ull;   // AZAE
static const uint64_t kTitleIdEU = 0x000500001019E600ull;   // AZAP
static const uint64_t kTitleIdJP = 0x000500001019C800ull;   // AZAJ

// WUPS calls the application lifecycle hooks for both UPID 2 (Wii U Menu) and
// UPID 15 (games). Keep the menu path deliberately inert: don't even query its
// title ID or initialize a TPHD subsystem there.
static const uint32_t kGameUPID = WUPS_FP_TARGET_PROCESS_GAME;

static volatile bool s_active = false;
static bool s_logStarted = false;

#ifdef TPHD_TOOLS_DEBUG
static bool s_presentHookSeen = false;
static bool s_vpadHookSeen = false;
static bool s_kpadHookSeen = false;
#endif

#ifdef TPHD_TOOLS_DEBUG
static void sdBootTrace(const char* fmt, ...)
{
    FILE* file = fopen("fs:/vol/external01/tphd_tools_aroma_boot_trace.txt", "a");
    if (!file)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
    fclose(file);
}
#else
#define sdBootTrace(...) ((void)0)
#endif

static bool isTphd(uint64_t tid)
{
    return tid == kTitleIdUS || tid == kTitleIdEU || tid == kTitleIdJP;
}

static void deactivate(const char* reason)
{
    bool wasActive = s_active;
    s_active = false;
    OSMemoryBarrier();

#ifdef TPHD_TOOLS_DEBUG
    OSReport("[tphd_tools.wps] %s upid=%u wasActive=%d\n",
             reason, (unsigned)OSGetUPID(), (int)wasActive);
    sdBootTrace("%s upid=%u wasActive=%d",
                reason, (unsigned)OSGetUPID(), (int)wasActive);
#else
    (void)reason;
    (void)wasActive;
#endif
}


// Lifecycle
INITIALIZE_PLUGIN()
{
    // Function replacements are static WUPS metadata below. They are limited to
    // game processes and their wrappers gate TPHD-specific work on s_active.
#ifdef TPHD_TOOLS_DEBUG
    OSReport("[tphd_tools.wps] INITIALIZE_PLUGIN\n");
    sdBootTrace("INITIALIZE_PLUGIN");
#endif
}

ON_APPLICATION_START()
{
    s_active = false;
    s_logStarted = false;
#ifdef TPHD_TOOLS_DEBUG
    s_presentHookSeen = false;
    s_vpadHookSeen = false;
    s_kpadHookSeen = false;
#endif

    uint32_t upid = OSGetUPID();
#ifdef TPHD_TOOLS_DEBUG
    sdBootTrace("ON_APPLICATION_START upid=%u", (unsigned)upid);
#endif
    if (upid != kGameUPID) {
#ifdef TPHD_TOOLS_DEBUG
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u ignored\n",
                 (unsigned)upid);
        sdBootTrace("ON_APPLICATION_START upid=%u ignored", (unsigned)upid);
#endif
        return;
    }

    uint64_t tid = OSGetTitleID();
    s_active = isTphd(tid);
    OSMemoryBarrier();
    if (s_active) {
        // Start the file logger on first present, after WUPS has completed the
        // application-start lifecycle. This mirrors the Cemu front end and
        // avoids creating a worker thread from inside the loader callback.
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u titleID=%016llx active=1\n",
                 (unsigned)upid, (unsigned long long)tid);
        sdBootTrace("ON_APPLICATION_START upid=%u titleID=%016llx active=1",
                    (unsigned)upid, (unsigned long long)tid);
    } else {
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u titleID=%016llx active=0\n",
                 (unsigned)upid, (unsigned long long)tid);
#ifdef TPHD_TOOLS_DEBUG
        sdBootTrace("ON_APPLICATION_START upid=%u titleID=%016llx active=0",
                    (unsigned)upid, (unsigned long long)tid);
#endif
    }
}

ON_APPLICATION_REQUESTS_EXIT()
{
    // Stop TPHD-specific work as soon as the title begins exiting. Waiting for
    // ON_APPLICATION_ENDS leaves a teardown window where GX2/input hooks can
    // still enter the overlay while Aroma is transitioning back to the menu.
    deactivate("ON_APPLICATION_REQUESTS_EXIT");
}

ON_APPLICATION_ENDS()
{
    // Title is tearing down: stop touching its GX2/input so we never run against
    // a dead context. (The overlay's static state is not torn down here; a clean
    // re-entry into TPHD without a system reboot may need a renderer reset -- see
    // README "Aroma ▸ Known limitations".)
    deactivate("ON_APPLICATION_ENDS");
    s_logStarted = false;
}

// Hooks -> shared overlay code

// Build the overlay once on the TV copy. If the configured target includes the
// GamePad, reuse that completed draw data when the DRC buffer is copied.
DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer, const GX2ColorBuffer *buffer, GX2ScanTarget scanTarget)
{
    if (s_active && scanTarget == GX2_SCAN_TARGET_TV && buffer) {
        if (!s_logStarted) {
            s_logStarted = true;
#ifdef TPHD_TOOLS_DEBUG
            sdBootTrace("GX2 TV hook first hit buffer=%p", buffer);
#endif
            Logger::StartNewLog();
            Logger::Log("[tphd_tools.wps] first TPHD present -- hooks active");
        }
#ifdef TPHD_TOOLS_DEBUG
        if (!s_presentHookSeen) {
            s_presentHookSeen = true;
            Logger::Log("[tphd_tools.wps] GX2 TV hook first hit buffer=%p", buffer);
        }
#endif
        Overlay::Present(const_cast<GX2ColorBuffer *>(buffer), nullptr);
    } else if (s_active && scanTarget == GX2_SCAN_TARGET_DRC && buffer) {
        Overlay::PresentGamePad(const_cast<GX2ColorBuffer *>(buffer));
    }

    real_GX2CopyColorBufferToScanBuffer(buffer, scanTarget);
}

// GamePad input: read for real, then stash + (when blocking) neutralize. We pass
// the real sample count so the filter only touches samples that exist.
DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffers, uint32_t count, VPADReadError *outError)
{
    int32_t samples = real_VPADRead(chan, buffers, count, outError);
    if (s_active && samples > 0 && buffers) {
#ifdef TPHD_TOOLS_DEBUG
        if (!s_vpadHookSeen) {
            s_vpadHookSeen = true;
            OSReport("[tphd_tools.wps] VPAD hook first hit samples=%d\n",
                     (int)samples);
        }
#endif
        Input::FilterVpad(buffers, samples);
    }
    return samples;
}

// Pro/Classic/Wiimote input: same wrap-the-read pattern.
DECL_FUNCTION(uint32_t, KPADReadEx, KPADChan chan, KPADStatus *data, uint32_t size, KPADError *error)
{
    uint32_t samples = real_KPADReadEx(chan, data, size, error);
    if (s_active && samples > 0 && data) {
#ifdef TPHD_TOOLS_DEBUG
        if (!s_kpadHookSeen) {
            s_kpadHookSeen = true;
            OSReport("[tphd_tools.wps] KPAD hook first hit samples=%u\n",
                     (unsigned)samples);
        }
#endif
        Input::FilterKpad(data, (int)samples);
    }
    return samples;
}

WUPS_MUST_REPLACE_FOR_PROCESS(GX2CopyColorBufferToScanBuffer, WUPS_LOADER_LIBRARY_GX2,
                              GX2CopyColorBufferToScanBuffer, WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead,
                              WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx,
                              WUPS_FP_TARGET_PROCESS_GAME);
