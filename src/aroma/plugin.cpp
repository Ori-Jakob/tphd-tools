// aroma/plugin.cpp -- the TPHD Tools front end for Aroma (WiiU Plugin System).
//
// Same overlay, different delivery. On Cemu we're an RPL the graphics-pack
// codecave acquires and calls through one export (src/cemu/main.cpp). On real
// hardware under Aroma we're a WUPS plugin (.wps) that installs its own
// function-replacement hooks at runtime -- the tpgz-style "patch the engine's
// own functions" approach -- with no codecave and no game-side patch file.
//
// We replace six system functions and route them into the SAME shared code the
// Cemu front end uses:
//   GX2CopyColorBufferToScanBuffer -> Overlay::Present(TV) / PresentGamePad(DRC)
//   GX2Init / context setters       -> private overlay context + game restore
//   VPADRead                       -> Input::FilterVpad   (GamePad)
//   KPADReadEx                     -> Input::FilterKpad   (Pro/Classic/Wiimote)
//
// Why GX2CopyColorBufferToScanBuffer and not GX2SwapScanBuffers: swap takes no
// arguments -- by then there's no surface to draw into. The copy-to-scan-buffer
// call is the last point that still hands us the finished TV GX2ColorBuffer, so
// we draw ImGui into it *before* the real copy carries it to scan-out. This is
// exactly the buffer the Cemu codecave handed us as g_abTvColorBuffer. Aroma
// draws under a private GX2ContextState and restores the title's context before
// chaining the copy, following GaryOderNichts/imgui_overlay_plugin's isolation
// pattern instead of leaving ImGui's registers bound in the game.
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
#include <gx2/context.h>        // GX2ContextState, GX2SetContextState
#include <gx2/swap.h>           // GX2CopyColorBufferToScanBuffer
#include <gx2/enum.h>           // GX2ScanTarget, GX2_SCAN_TARGET_TV
#include <gx2/mem.h>            // GX2Invalidate
#include <gx2/state.h>          // GX2Init, GX2Flush
#include <gx2/surface.h>        // GX2ColorBuffer
#include <memory/mappedmemory.h>
#include <vpad/input.h>         // VPADRead, VPADStatus, VPADReadError
#include <padscore/kpad.h>      // KPADReadEx, KPADStatus, KPADError

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "overlay.h"
#include "input.h"
#include "logger.h"
#include "version.h"
#include "game/d_stage.h"
#include "tools/save_state.h"
#ifdef TPHD_TOOLS_EXPERIMENTAL
#include "tools/boss_practice.h"
#endif
#include "tools/modern_camera.h"
#include "debug/debug_save.h"


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
static bool s_tphdApplication = false;
static bool s_logStarted = false;
static GX2ContextState* s_overlayContext = nullptr;
static GX2ContextState* s_gameContext = nullptr;
static bool s_overlayContextReady = false;
static uint32_t s_presentBlockedFrames = 0;

#ifdef TPHD_TOOLS_DEBUG
static bool s_presentHookSeen = false;
static bool s_vpadHookSeen = false;
static bool s_kpadHookSeen = false;
static bool s_gameContextSeen = false;
static bool s_contextWaitReported = false;
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

// dScnPly's phase table lives in Zelda.rpx data, so Aroma can install this
// title-specific hook only after the TPHD title gate succeeds. Saving and
// chaining the current slot also cooperates with a hook installed earlier by
// another plugin instead of assuming the slot still contains Nintendo's
// original function.
static dScnPly_phase1_t s_realScenePhase1 = nullptr;

static volatile dScnPly_phase1_t* scenePhase1Slot()
{
    return reinterpret_cast<volatile dScnPly_phase1_t*>(
        static_cast<uintptr_t>(GAME_ADDR_dScnPly_phase1_slot));
}

static int scenePhase1Hook(void* scene)
{
    dScnPly_phase1_t real = s_realScenePhase1;
    if (s_active)
        Tools::SaveState::OnScenePhase1();

    // Installation refuses a null slot, so this is only a last-ditch guard
    // against another plugin unexpectedly rewriting our saved chain target.
    if (!real)
        real = reinterpret_cast<dScnPly_phase1_t>(
            static_cast<uintptr_t>(GAME_ADDR_dScnPly_phase1));
    return real(scene);
}

static bool installScenePhase1Hook()
{
    volatile dScnPly_phase1_t* slot = scenePhase1Slot();
    if (s_realScenePhase1 && *slot == scenePhase1Hook)
        return true;

    // A missed application-end callback can leave our saved chain pointer from
    // the previous RPX lifetime. Re-read the live slot instead of trusting it.
    s_realScenePhase1 = nullptr;
    dScnPly_phase1_t current = *slot;
    if (!current)
        return false;

    if (current == scenePhase1Hook) {
        // Defensive recovery for an unexpected stale slot.
        current = reinterpret_cast<dScnPly_phase1_t>(
            static_cast<uintptr_t>(GAME_ADDR_dScnPly_phase1));
    }

    s_realScenePhase1 = current;
    *slot = scenePhase1Hook;
    DCFlushRange((void*)slot, sizeof(*slot));
    OSMemoryBarrier();
    return *slot == scenePhase1Hook;
}

static void restoreScenePhase1Hook()
{
    dScnPly_phase1_t real = s_realScenePhase1;
    if (!real)
        return;

    volatile dScnPly_phase1_t* slot = scenePhase1Slot();
    // Do not overwrite a hook installed after ours.
    if (*slot == scenePhase1Hook) {
        *slot = real;
        DCFlushRange((void*)slot, sizeof(*slot));
        OSMemoryBarrier();
    }
    s_realScenePhase1 = nullptr;
}

// The room phase at 0x02ac3f68 allocates dSv_zone_c and parses room.dzr. Restore
// mMemory/mDan before entering it (the same records a normal getSave supplies),
// then merge dynamically allocated zone bytes after it returns.
static dScnRoom_zone_phase_t s_realRoomZonePhase = nullptr;

static volatile dScnRoom_zone_phase_t* roomZonePhaseSlot()
{
    return reinterpret_cast<volatile dScnRoom_zone_phase_t*>(
        static_cast<uintptr_t>(GAME_ADDR_dScnRoom_zone_phase_slot));
}

static int roomZonePhaseHook(void* roomScene)
{
    dScnRoom_zone_phase_t real = s_realRoomZonePhase;
    if (!real) {
        real = reinterpret_cast<dScnRoom_zone_phase_t>(
            static_cast<uintptr_t>(GAME_ADDR_dScnRoom_zone_phase));
    }

    if (s_active)
        Tools::SaveState::OnRoomCreateBegin(roomScene);
    const int result = real(roomScene);
    if (s_active)
        Tools::SaveState::OnRoomZoneReady(roomScene);
    return result;
}

static bool installRoomZonePhaseHook()
{
    volatile dScnRoom_zone_phase_t* slot = roomZonePhaseSlot();
    if (s_realRoomZonePhase && *slot == roomZonePhaseHook)
        return true;

    s_realRoomZonePhase = nullptr;
    dScnRoom_zone_phase_t current = *slot;
    if (!current)
        return false;
    if (current == roomZonePhaseHook) {
        current = reinterpret_cast<dScnRoom_zone_phase_t>(
            static_cast<uintptr_t>(GAME_ADDR_dScnRoom_zone_phase));
    }

    s_realRoomZonePhase = current;
    *slot = roomZonePhaseHook;
    DCFlushRange((void*)slot, sizeof(*slot));
    OSMemoryBarrier();
    return *slot == roomZonePhaseHook;
}

static void restoreRoomZonePhaseHook()
{
    dScnRoom_zone_phase_t real = s_realRoomZonePhase;
    if (!real)
        return;

    volatile dScnRoom_zone_phase_t* slot = roomZonePhaseSlot();
    if (*slot == roomZonePhaseHook) {
        *slot = real;
        DCFlushRange((void*)slot, sizeof(*slot));
        OSMemoryBarrier();
    }
    s_realRoomZonePhase = nullptr;
}

static void deactivate(const char* reason)
{
    bool wasActive = s_active;
    uint32_t upid = OSGetUPID();
    s_active = false;
    OSMemoryBarrier();
    s_gameContext = nullptr;
    s_overlayContextReady = false;
    // Never dereference a Zelda.rpx address from a Wii U Menu lifecycle call,
    // even if stale plugin state somehow survives a process transition.
    if (wasActive && upid == kGameUPID) {
        restoreScenePhase1Hook();
        restoreRoomZonePhaseHook();
    } else if (upid != kGameUPID) {
        s_realScenePhase1 = nullptr;
        s_realRoomZonePhase = nullptr;
    }

#ifdef TPHD_TOOLS_DEBUG
    OSReport("[tphd_tools.wps] %s upid=%u wasActive=%d\n",
             reason, (unsigned)upid, (int)wasActive);
    if (upid == kGameUPID && s_tphdApplication)
        sdBootTrace("%s upid=%u wasActive=%d",
                    reason, (unsigned)upid, (int)wasActive);
#else
    (void)reason;
    (void)wasActive;
#endif
}

// GX2 context isolation
//
// ImGui changes shaders, blend/depth state, targets, viewport, and scissor.
// Capture the context selected by TPHD, draw under a plugin-owned context, then
// switch back before the real scan-buffer copy. Calls through real_* preserve
// WUPS's replacement chain when another plugin hooks the same GX2 functions.
static bool setupOverlayContext();

DECL_FUNCTION(void, GX2SetContextState, GX2ContextState* state)
{
    real_GX2SetContextState(state);
    if (s_active && state && state != s_overlayContext) {
        s_gameContext = state;
#ifdef TPHD_TOOLS_DEBUG
        if (!s_gameContextSeen) {
            s_gameContextSeen = true;
            OSReport("[tphd_tools.wps] captured game GX2 context=%p via set\n",
                     state);
            sdBootTrace("captured game GX2 context=%p via set", state);
        }
#endif
    }
}

DECL_FUNCTION(void, GX2SetupContextStateEx, GX2ContextState* state, BOOL unk1)
{
    real_GX2SetupContextStateEx(state, unk1);
    if (s_active && state && state != s_overlayContext) {
        s_gameContext = state;
#ifdef TPHD_TOOLS_DEBUG
        if (!s_gameContextSeen) {
            s_gameContextSeen = true;
            OSReport("[tphd_tools.wps] captured game GX2 context=%p via setup\n",
                     state);
            sdBootTrace("captured game GX2 context=%p via setup", state);
        }
#endif
    }
}

DECL_FUNCTION(void, GX2Init, uint32_t* attributes)
{
    real_GX2Init(attributes);
    if (s_active) {
        // A fresh GX2 lifetime invalidates both shadow states. Do not set up our
        // state here: GX2SetupContextStateEx makes that state current, and the
        // title has not necessarily supplied a context we can restore yet.
        s_gameContext = nullptr;
        s_overlayContextReady = false;
#ifdef TPHD_TOOLS_DEBUG
        OSReport("[tphd_tools.wps] GX2Init observed; overlay setup deferred\n");
        sdBootTrace("GX2Init observed; overlay setup deferred");
#endif
    }
}

static bool setupOverlayContext()
{
    if (s_overlayContextReady)
        return true;
    if (!s_overlayContext)
        return false;

    // Use real_* so our own setup is never mistaken for the game's current
    // context by the capture hook above.
    real_GX2SetupContextStateEx(s_overlayContext, GX2_TRUE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU,
                  s_overlayContext, sizeof(*s_overlayContext));
    s_overlayContextReady = true;
#ifdef TPHD_TOOLS_DEBUG
    OSReport("[tphd_tools.wps] overlay GX2 context ready=%p\n",
             s_overlayContext);
    sdBootTrace("overlay GX2 context ready=%p", s_overlayContext);
#endif
    return true;
}

static GX2ContextState* beginOverlayDraw()
{
    // Snapshot the exact state active at hook entry. Never initialize/bind the
    // overlay context until we know we have something valid to restore.
    GX2ContextState* restoreContext = s_gameContext;
    if (!restoreContext || !setupOverlayContext()) {
#ifdef TPHD_TOOLS_DEBUG
        if (!s_contextWaitReported) {
            s_contextWaitReported = true;
            OSReport("[tphd_tools.wps] overlay waiting for GX2 context "
                     "overlay=%p ready=%d game=%p\n",
                     s_overlayContext, (int)s_overlayContextReady,
                     s_gameContext);
            sdBootTrace("overlay waiting for GX2 context overlay=%p "
                        "ready=%d game=%p",
                        s_overlayContext, (int)s_overlayContextReady,
                        s_gameContext);
        }
#endif
        return nullptr;
    }

    real_GX2SetContextState(s_overlayContext);
    return restoreContext;
}

static void endOverlayDraw(GX2ContextState* restoreContext)
{
    // Submit ImGui's commands before rebinding the title's shadow state.
    GX2Flush();
    real_GX2SetContextState(restoreContext);
}


// Lifecycle
INITIALIZE_PLUGIN()
{
    // Function replacements are static WUPS metadata below. They are limited to
    // game processes and their wrappers gate TPHD-specific work on s_active.
    s_overlayContext = static_cast<GX2ContextState*>(
        MEMAllocFromMappedMemoryForGX2Ex(
            sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT));
    if (!s_overlayContext)
        OSReport("[tphd_tools.wps] failed to allocate overlay GX2 context\n");
#ifdef TPHD_TOOLS_DEBUG
    OSReport("[tphd_tools.wps] INITIALIZE_PLUGIN overlayContext=%p\n",
             s_overlayContext);
#endif
}

ON_APPLICATION_START()
{
    s_active = false;
    s_logStarted = false;
    s_gameContext = nullptr;
    s_overlayContextReady = false;
    s_tphdApplication = false;
    s_presentBlockedFrames = 0;
#ifdef TPHD_TOOLS_DEBUG
    s_presentHookSeen = false;
    s_vpadHookSeen = false;
    s_kpadHookSeen = false;
    s_gameContextSeen = false;
    s_contextWaitReported = false;
#endif

    uint32_t upid = OSGetUPID();
    if (upid != kGameUPID) {
#ifdef TPHD_TOOLS_DEBUG
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u ignored\n",
                 (unsigned)upid);
#endif
        return;
    }

    // The logger's worker thread (if any) belonged to the previous game process
    // and died with it; reset so the first log in this process starts a new one.
    Logger::OnApplicationStart();
    // Same lifecycle rule for the loaders: their worker threads died with the
    // process, and any in-flight load state (a pending warp, a handed-off
    // snapshot, a wedged busy flag) belongs to the previous session and must
    // never leak into this one.
    Tools::SaveState::OnApplicationStart();
#ifdef TPHD_TOOLS_EXPERIMENTAL
    Tools::BossPractice::OnApplicationStart();
#endif
    Debug::DebugSave::OnApplicationStart();
    Tools::ModernCamera::OnApplicationStart();

    uint64_t tid = OSGetTitleID();
    s_active = isTphd(tid);
    s_tphdApplication = s_active;
    OSMemoryBarrier();
    if (s_active) {
        // Retry the overlay-context allocation if INITIALIZE_PLUGIN failed
        // (e.g. the mapped-memory module was briefly out of space). Without a
        // context the overlay can never draw.
        if (!s_overlayContext) {
            s_overlayContext = static_cast<GX2ContextState*>(
                MEMAllocFromMappedMemoryForGX2Ex(
                    sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT));
            if (!s_overlayContext)
                OSReport("[tphd_tools.wps] overlay GX2 context alloc retry "
                         "failed\n");
        }
        Overlay::OnApplicationStart();
        bool phaseHookInstalled = installScenePhase1Hook();
        bool roomHookInstalled = installRoomZonePhaseHook();
        // Start the file logger on first present, after WUPS has completed the
        // application-start lifecycle. This mirrors the Cemu front end and
        // avoids creating a worker thread from inside the loader callback.
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u titleID=%016llx "
                 "active=1 phase1Hook=%d roomHook=%d\n",
                 (unsigned)upid, (unsigned long long)tid,
                 (int)phaseHookInstalled, (int)roomHookInstalled);
        sdBootTrace("ON_APPLICATION_START upid=%u titleID=%016llx active=1 "
                    "phase1Hook=%d roomHook=%d",
                    (unsigned)upid, (unsigned long long)tid,
                    (int)phaseHookInstalled, (int)roomHookInstalled);
    } else {
        OSReport("[tphd_tools.wps] ON_APPLICATION_START upid=%u titleID=%016llx active=0\n",
                 (unsigned)upid, (unsigned long long)tid);
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
    // Stop touching title-owned state first, then discard GPU-facing ImGui
    // objects so a later TPHD launch starts from the new GX2 lifetime.
    deactivate("ON_APPLICATION_ENDS");
    if (s_tphdApplication)
        Overlay::OnApplicationEnd();
    s_tphdApplication = false;
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
        if (GX2ContextState* restoreContext = beginOverlayDraw()) {
            Overlay::Present(const_cast<GX2ColorBuffer *>(buffer), nullptr);
            endOverlayDraw(restoreContext);
        } else {
            // Can't draw this frame (no game context captured yet, or the
            // overlay context allocation failed). Still consume this frame's
            // input events: otherwise a pressed hotkey latches the input
            // drain with no Present to ever release it, permanently zeroing
            // the player's controls.
            Input::BeginFrame();
            if (++s_presentBlockedFrames == 600)
                Logger::LogWarn("[tphd_tools.wps] overlay still waiting for a "
                                "GX2 context after 600 presents (overlay=%p "
                                "ready=%d game=%p)",
                                s_overlayContext, (int)s_overlayContextReady,
                                s_gameContext);
        }
    } else if (s_active && scanTarget == GX2_SCAN_TARGET_DRC && buffer &&
               Overlay::WantsGamePadDraw()) {
        // Gated so TV-only configurations don't pay a context switch + flush
        // on every DRC copy just to draw nothing.
        if (GX2ContextState* restoreContext = beginOverlayDraw()) {
            Overlay::PresentGamePad(const_cast<GX2ColorBuffer *>(buffer));
            endOverlayDraw(restoreContext);
        }
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
WUPS_MUST_REPLACE_FOR_PROCESS(GX2SetContextState, WUPS_LOADER_LIBRARY_GX2,
                              GX2SetContextState, WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(GX2SetupContextStateEx, WUPS_LOADER_LIBRARY_GX2,
                              GX2SetupContextStateEx, WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(GX2Init, WUPS_LOADER_LIBRARY_GX2, GX2Init,
                              WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead,
                              WUPS_FP_TARGET_PROCESS_GAME);
WUPS_MUST_REPLACE_FOR_PROCESS(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx,
                              WUPS_FP_TARGET_PROCESS_GAME);
