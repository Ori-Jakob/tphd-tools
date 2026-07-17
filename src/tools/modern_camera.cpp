 // Per engine, the wrapper does one of two things:
//  - chase (entry 1), and ride (entry 8) when the Epona option is on:
//    the full direction override described below;
//  - every other engine (first person, lock-on, talk, cutscene, ...):
//    passthrough, plus the FOV scale when "global FOV" is enabled.
//
// Direction override (matching dusklight):
//  - Until the right stick is touched, nothing changes (the engine's own
//    auto-follow direction continuously re-seeds ours).
//  - Once touched, "manual mode" latches: the stick accumulates yaw/pitch,
//    pitch clamped, and the auto-rotate never fights the player again --
//    until a non-owned camera runs (lock-on, first person, cutscene...),
//    which drops the latch so the next owned frame starts vanilla.
//  - The engine's center, follow distance R (zoom, walls), and transitions
//    stay the game's own.
//
// Input comes from the camera's OWN pad cache (mPadInfo.mCStick), read at
// wrapper entry and then ZEROED (with its Y state machine) before the real
// engine runs: TPHD's engines have native right-stick camera control (turn +
// flick-zoom, the HD rubber-band response) that would otherwise fight the
// override. The zoom gear is pinned to its standard level while owned, since
// the flick that steps it can fire before the dispatch. The override runs in
// normal follow, the Z-recenter, and the locked-on modes (vanilla allows
// stick camera movement during Z-target, so the modern control does too);
// entering a new mode drops the manual latch so each mode's own framing --
// the Z-press recenter behind Link, the lock-on target framing -- wins until
// the stick is touched again. Other modes (charge, first person, event, ...)
// pass through vanilla.
//
// FOV: engines lerp mViewCache.mFovy toward the style target each frame
// reading their own previous value, so a naive multiply would feed back
// (divergent for scales > 1). The wrapper restores the unscaled value before
// any engine runs and re-applies the scale after.
#include "tools/modern_camera.h"

#include "imgui.h"
#include "overlay.h"            // ov::g_menuVisible
#include "game/game.h"

#include <math.h>

#include <coreinit/cache.h>     // DCFlushRange / OSMemoryBarrier -- slot patch
#include <coreinit/time.h>      // OSGetTick -- dispatch cost instrumentation
#include <coreinit/systeminfo.h>

#include "utils/logger.h"       // TPHD_BREADCRUMB (debug builds only)

namespace Tools {
namespace ModernCamera {

// ---- tuning -------------------------------------------------------------------
// dusklight: angle += stick * magnitude * sensitivity * 5 degrees per frame,
// inclination clamped to [-30, +50] degrees.
static const float kDegPerFrame   = 5.0f;
static const float kPitchRatio    = 0.7f;   // base Y speed relative to X
static const float kInclMinDeg    = -30.0f;
static const float kInclMaxDeg    = 50.0f;
static const float kStickDeadzone = 0.10f;
static const float kFovScaleMin   = 0.60f;
static const float kFovScaleMax   = 1.50f;
static const float kSensMin       = 0.2f;
static const float kSensMax       = 3.0f;

// ---- settings (persisted via config.cpp) ----------------------------------------
static bool  s_enabled   = false;
static float s_sensX     = 1.0f;
static float s_sensY     = 1.0f;
static float s_fovScale  = 1.0f;
static bool  s_globalFov = false;  // scale FOV under every camera, not just owned
static bool  s_horse     = false;  // own the ride (Epona) camera too
static bool  s_invertX   = false;
static bool  s_invertY   = false;

// ---- state -----------------------------------------------------------------------
static bool  s_manual   = false;   // stick touched; direction is player-owned
static float s_yawDeg   = 0.0f;    // accumulated azimuth, degrees
static float s_pitchDeg = 0.0f;    // accumulated inclination, degrees
static bool  s_fovDirty = false;   // s_fovCam's fovy currently holds a scaled value
static float s_fovUnscaled = 0.0f;
static void* s_fovCam   = nullptr; // which camera instance holds the scaled fovy
static int   s_hookDepth = 0;      // engines re-dispatch through the table (event
                                   // camera); only the outermost hook may work
// Dispatch counters: written on the game thread and read on the present thread
// (Tick's unlatch check and the menu readout). Use atomic builtins so this is not
// a C++ data race. The hook publishes s_overrideRuns BEFORE s_hookRuns; Tick's
// acquire-load of s_hookRuns followed by the override load therefore cannot see
// a new hook run whose override count is still missing (that torn observation
// would drop the manual latch on an OWNED dispatch and visibly hitch the camera).
static uint32_t s_hookRuns = 0;      // any outermost engine dispatch
static uint32_t s_overrideRuns = 0;  // dispatches where direction was owned
static uint32_t s_lastHookSeen = 0;           // Tick-side counter shadows
static uint32_t s_lastOverrideSeen = 0;

static inline uint32_t loadHookRuns()
{
    return __atomic_load_n(&s_hookRuns, __ATOMIC_ACQUIRE);
}

static inline uint32_t loadOverrideRuns()
{
    return __atomic_load_n(&s_overrideRuns, __ATOMIC_RELAXED);
}

static inline void countDispatch(bool active)
{
    if (active)
        __atomic_fetch_add(&s_overrideRuns, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_hookRuns, 1u, __ATOMIC_RELEASE);
}

static inline void resetDispatchCounts()
{
    __atomic_store_n(&s_overrideRuns, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_hookRuns, 0u, __ATOMIC_RELEASE);
}

// Dispatch cost, in OS ticks (menu readout; written on the game thread).
static volatile uint32_t s_perfSelfTicks = 0;    // our pre+post work, last dispatch
static volatile uint32_t s_perfSelfMax = 0;
static volatile uint32_t s_perfEngineTicks = 0;  // the real engine, last dispatch
static volatile uint32_t s_perfEngineMax = 0;

static dCamEngine_t s_realEngine[DCAM_ENGINE_COUNT] = {};
static bool s_hookInstalled = false;

// Menu diagnostics, captured in the wrappers so the readout shows what the
// override actually saw (not what the present thread re-derives).
static bool  s_dbgActive = false;
static float s_dbgStickX = 0.0f;
static float s_dbgStickY = 0.0f;
static int   s_dbgGear = 0;
static int   s_dbgEngine = -1;     // last engine index actually dispatched
static int   s_dbgMode = -1;       // camera mode at that dispatch (0 = normal)
static int   s_lastDirMode = -1;   // mode seen by the last direction-owning
                                   // dispatch (latch drops on any change)

bool IsEnabled()               { return s_enabled; }
float GetSensitivityX()        { return s_sensX; }
float GetSensitivityY()        { return s_sensY; }
float GetFovScale()            { return s_fovScale; }
bool IsGlobalFovEnabled()      { return s_globalFov; }
void SetGlobalFovEnabled(bool e) { s_globalFov = e; }
bool IsHorseEnabled()          { return s_horse; }
void SetHorseEnabled(bool e)   { s_horse = e; }
bool IsInvertXEnabled()        { return s_invertX; }
bool IsInvertYEnabled()        { return s_invertY; }
void SetInvertXEnabled(bool e) { s_invertX = e; }
void SetInvertYEnabled(bool e) { s_invertY = e; }

void SetEnabled(bool e)
{
    // Never carry a manual direction across a disable: re-enabling later
    // would snap the camera to angles accumulated in a different situation.
    if (!e)
        s_manual = false;
    s_enabled = e;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void SetSensitivityX(float s) { s_sensX = clampf(s, kSensMin, kSensMax); }
void SetSensitivityY(float s) { s_sensY = clampf(s, kSensMin, kSensMax); }
void SetFovScale(float s)     { s_fovScale = clampf(s, kFovScaleMin, kFovScaleMax); }

// ---- the override (runs on the game thread, inside the camera update) -----------
// Something else may own the camera this frame: an event (cutscene
// camera-play, including via currentEvCamera which dispatches through this
// same table) or the fly cam (which freezes the game and drives at/eye itself
// via the camera-play flag).
static bool overrideActive()
{
    return s_enabled && !dEvt_isEventRunning() &&
           *(volatile u8*)GAME_ADDR_decoupleFlag == 0;
}

static void applyFovScale(u8* cam)
{
    float* fovy = (float*)(cam + DCAM_OFF_VIEW_FOVY);
    s_fovUnscaled = *fovy;
    s_fovCam = cam;
    if (s_fovScale != 1.0f) {
        *fovy = clampf(s_fovUnscaled * s_fovScale, 20.0f, 120.0f);
        s_fovDirty = true;
    }
}

static void applyOverride(u8* cam, float stickX, float stickY)
{
    volatile s16* incl = (volatile s16*)(cam + DCAM_OFF_VIEW_INCL);
    volatile s16* azim = (volatile s16*)(cam + DCAM_OFF_VIEW_AZIMUTH);

    applyFovScale(cam);

    // Not manual yet: keep re-seeding our accumulators from the engine's own
    // (auto-follow) direction so the first stick touch continues seamlessly.
    if (!s_manual) {
        s_yawDeg   = (float)*azim * (360.0f / 65536.0f);
        s_pitchDeg = (float)*incl * (360.0f / 65536.0f);
    }

    // While the menu is open the sticks belong to the UI (the engine's copy
    // was zeroed either way, so its native control never runs).
    if (!ov::g_menuVisible) {
        float rx = stickX;
        float ry = stickY;
        if (rx > -kStickDeadzone && rx < kStickDeadzone) rx = 0.0f;
        if (ry > -kStickDeadzone && ry < kStickDeadzone) ry = 0.0f;
        if (rx != 0.0f || ry != 0.0f) {
            s_manual = true;
            // dusklight: azimuth += x * sens * 5 deg; stick up looks up
            // (camera sinks) unless inverted.
            s_yawDeg   += (s_invertX ? -rx : rx) * kDegPerFrame * s_sensX;
            s_pitchDeg += (s_invertY ? ry : -ry) * kDegPerFrame * kPitchRatio * s_sensY;
            if (s_yawDeg > 180.0f)
                s_yawDeg -= 360.0f;
            else if (s_yawDeg < -180.0f)
                s_yawDeg += 360.0f;
            s_pitchDeg = clampf(s_pitchDeg, kInclMinDeg, kInclMaxDeg);
        }
    }

    if (!s_manual)
        return;   // vanilla frame: leave the engine's direction untouched

    // Rebuild the eye from the engine's center + follow distance with our
    // direction (the exact cSGlobe::Xyz the engine itself uses). The caller's
    // wall/bump check still runs after the dispatch, so collision applies to
    // this eye too.
    *azim = (s16)(int)(s_yawDeg * (65536.0f / 360.0f));
    *incl = (s16)(int)(s_pitchDeg * (65536.0f / 360.0f));

    const float r    = *(float*)(cam + DCAM_OFF_VIEW_R);
    const float yawR = s_yawDeg * 0.01745329252f;
    const float pitR = s_pitchDeg * 0.01745329252f;
    const float cp   = cosf(pitR);
    const cXyz* center = (const cXyz*)(cam + DCAM_OFF_VIEW_CENTER);
    cXyz* eye = (cXyz*)(cam + DCAM_OFF_VIEW_EYE);
    eye->x = center->x + r * cp * sinf(yawR);
    eye->y = center->y + r * sinf(pitR);
    eye->z = center->z + r * cp * cosf(yawR);
}

static int engineHookCommon(int idx, void* camera, int style)
{
    dCamEngine_t real = s_realEngine[idx];
    if (!real) {
        // Unreachable when installed via installHooks (it refuses null
        // slots); recover the one address we know rather than jump to null.
        if (idx != DCAM_ALG_CHASE)
            return 0;
        real = (dCamEngine_t)GAME_ADDR_dCamera_chaseCamera;
    }

    // Engines re-dispatch through this same table (the event engine runs the
    // event camera, which selects and dispatches another engine mid-call).
    // Only the OUTERMOST invocation may do FOV/direction work: if the inner
    // hook scaled the FOV, the outer hook would treat that scaled value as
    // unscaled and scale it again -- and bake the compounding into the next
    // frame's restore.
    if (s_hookDepth > 0) {
        ++s_hookDepth;
        int nested = real(camera, style);
        --s_hookDepth;
        return nested;
    }
    ++s_hookDepth;

    uint32_t tickEnter = (uint32_t)OSGetTick();
    u8* cam = (u8*)camera;
    // Give the engine back the unscaled FOV it expects to lerp from. The
    // scaled value lives in one specific camera instance; a different
    // instance just drops the stale flag.
    if (s_fovDirty) {
        if (camera == s_fovCam)
            *(float*)(cam + DCAM_OFF_VIEW_FOVY) = s_fovUnscaled;
        s_fovDirty = false;
    }

    const bool wantsDirection =
        idx == DCAM_ALG_CHASE || (idx == DCAM_ALG_RIDE && s_horse);
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool active = false;
    if (wantsDirection) {
        // Own the direction in normal follow, the Z-recenter, and the
        // locked-on modes -- vanilla lets the stick move the camera during
        // Z-target, so the modern control should too (and owning them keeps
        // the native stick-Y flick-zoom from firing there). Entering a NEW
        // mode drops the latch, so each mode's own framing wins first: the
        // Z-press recenter behind Link (1) and the lock-on target framing
        // (2/8) play out while we re-seed from the engine, and the first
        // stick touch after that takes over. Charge (7) and the rest pass
        // through vanilla. The mode is committed by the mode control
        // immediately before this dispatch, so it never lags.
        const int mode = dCam_getMode(camera);
        if (mode != s_lastDirMode) {
            s_manual = false;
            s_lastDirMode = mode;
        }
        const bool modernMode = mode == DCAM_MODE_FOLLOW ||
                                mode == DCAM_MODE_RESET ||
                                mode == DCAM_MODE_LOCK ||
                                mode == DCAM_MODE_RIDE_LOCK;
        active = overrideActive() && modernMode;
        if (active) {
            // Take the right stick from the camera's own pad cache, then
            // zero the whole C-stick window -- the stick block AND the Y
            // state machine behind it -- so none of the engine's native
            // stick responses (turn/recenter, flick-zoom) ever run. The pad
            // fill and the mode/attention logic already consumed the real
            // values before this dispatch (R3 first person is unaffected),
            // and the fill rewrites the window next frame.
            stickX = *(float*)(cam + DCAM_OFF_PAD_CSTICK_X);
            stickY = *(float*)(cam + DCAM_OFF_PAD_CSTICK_Y);
            u32* window = (u32*)(cam + DCAM_OFF_PAD_CSTICK);
            for (u32 i = 0; i < DCAM_PAD_CSTICK_WINDOW / 4; ++i)
                window[i] = 0;

            // Pin the zoom gear to its standard level. The flick that steps
            // it may fire before the dispatch, so starving the engine's
            // stick copy alone can't stop it; a CAPTURED pin went stale when
            // the tool was toggled mid-zoom. The engine plays the (at most
            // one) gear change as its normal smooth zoom.
            volatile int* gear = (volatile int*)(cam + DCAM_OFF_GEAR);
            s_dbgGear = *gear;
            *gear = 0;
        } else {
            s_manual = false;
        }
        s_dbgActive = active;
        s_dbgStickX = stickX;
        s_dbgStickY = stickY;
    }

    uint32_t tickPre = (uint32_t)OSGetTick();
    int result = real(camera, style);
    uint32_t tickPost = (uint32_t)OSGetTick();

    countDispatch(active);
    s_dbgEngine = idx;
    s_dbgMode = dCam_getMode(camera);

    if (active) {
        applyOverride(cam, stickX, stickY);
    } else if (s_enabled && s_globalFov) {
        // Global FOV: keep the scale under first person, lock-on, talk,
        // cutscenes -- every camera.
        applyFovScale(cam);
    }
    --s_hookDepth;

    uint32_t tickExit = (uint32_t)OSGetTick();
    uint32_t self = (tickPre - tickEnter) + (tickExit - tickPost);
    uint32_t engine = tickPost - tickPre;
    s_perfSelfTicks = self;
    s_perfEngineTicks = engine;
    if (self > s_perfSelfMax)
        s_perfSelfMax = self;
    if (engine > s_perfEngineMax)
        s_perfEngineMax = engine;
    return result;
}

// One thin trampoline per engine-table entry, so each patched slot knows its
// own index and chain target.
template <int N>
static int engineHookN(void* camera, int style)
{
    return engineHookCommon(N, camera, style);
}

static const dCamEngine_t s_hooks[DCAM_ENGINE_COUNT] = {
    engineHookN<0>,  engineHookN<1>,  engineHookN<2>,  engineHookN<3>,
    engineHookN<4>,  engineHookN<5>,  engineHookN<6>,  engineHookN<7>,
    engineHookN<8>,  engineHookN<9>,  engineHookN<10>, engineHookN<11>,
    engineHookN<12>, engineHookN<13>, engineHookN<14>, engineHookN<15>,
    engineHookN<16>, engineHookN<17>, engineHookN<18>, engineHookN<19>,
};

// ---- engine-table slot patches (same mechanism as the phase_1 hook) --------------
static bool installHooks()
{
    if (s_hookInstalled)
        return true;

    // All-or-nothing: refuse if any slot is null (table not initialized yet).
    for (int i = 0; i < DCAM_ENGINE_COUNT; ++i) {
        if (!*dCam_engineSlot(i))
            return false;
    }
#ifdef TPHD_TOOLS_DEBUG
    uint32_t t0 = (uint32_t)OSGetTick();
#endif
    for (int i = 0; i < DCAM_ENGINE_COUNT; ++i) {
        volatile dCamEngine_t* slot = dCam_engineSlot(i);
        dCamEngine_t current = *slot;
        // Cooperate with an earlier foreign hook; never chain to ourselves
        // (a stale slot from an interrupted lifetime keeps the old chain).
        if (current != s_hooks[i])
            s_realEngine[i] = current;
        *slot = s_hooks[i];
    }
    DCFlushRange((void*)dCam_engineSlot(0),
                 DCAM_ENGINE_COUNT * DCAM_ENGINE_ENTRY_STRIDE);
    OSMemoryBarrier();
    s_hookInstalled = true;
    s_perfSelfMax = 0;
    s_perfEngineMax = 0;
#ifdef TPHD_TOOLS_DEBUG
    TPHD_BREADCRUMB("mcam: engine hooks installed (%u ticks)",
                    (unsigned)((uint32_t)OSGetTick() - t0));
#endif
    return true;
}

static void removeHooks()
{
    if (!s_hookInstalled)
        return;
    for (int i = 0; i < DCAM_ENGINE_COUNT; ++i) {
        volatile dCamEngine_t* slot = dCam_engineSlot(i);
        // Do not overwrite a hook installed after ours.
        if (*slot == s_hooks[i] && s_realEngine[i])
            *slot = s_realEngine[i];
        s_realEngine[i] = nullptr;
    }
    DCFlushRange((void*)dCam_engineSlot(0),
                 DCAM_ENGINE_COUNT * DCAM_ENGINE_ENTRY_STRIDE);
    OSMemoryBarrier();
    s_hookInstalled = false;
    s_manual = false;
    s_fovDirty = false;   // the engines own whatever fovy holds now
    s_fovCam = nullptr;
    TPHD_BREADCRUMB("mcam: engine hooks removed");
}

void Tick()
{
    if (s_enabled) {
        if (!s_hookInstalled)
            installHooks();
    } else if (s_hookInstalled) {
        removeHooks();
    }

    // dusklight resets its manual latch at the dispatch site whenever a
    // non-owned engine is selected. Our equivalent signal: an engine
    // dispatched (s_hookRuns advanced) without the direction being owned
    // (s_overrideRuns didn't) -- first person, the LockOn recenter, a
    // cutscene, ... ran the camera, so the next owned frame must start
    // vanilla. Deliberately NOT "no owned dispatch since the last present":
    // the present callback and the game's camera update are unsynchronized
    // threads, and two presents slipping between two dispatches (pipeline
    // jitter, a dropped frame) would clear the latch mid-steer -- one frame
    // of auto-rotate plus a reseed, felt as a random camera hitch. And NOT
    // dCam_getAlgorithm(): mCurrentStyle can lag the dispatched style, and a
    // false mismatch would clear the latch every frame -- exactly the
    // rubber-band behavior the override exists to remove. Hook count is read
    // before override count (the hook writes them in the opposite order) so
    // a dispatch landing between the reads can't fake a non-owned run.
    uint32_t hookNow = loadHookRuns();
    uint32_t overrideNow = loadOverrideRuns();
    if (s_hookInstalled && hookNow != s_lastHookSeen &&
        overrideNow == s_lastOverrideSeen)
        s_manual = false;
    s_lastHookSeen = hookNow;
    s_lastOverrideSeen = overrideNow;
}

void OnApplicationStart()
{
    // The engine table is fresh game data in the new process: nothing is
    // installed anymore no matter what the statics say.
    s_hookInstalled = false;
    for (int i = 0; i < DCAM_ENGINE_COUNT; ++i)
        s_realEngine[i] = nullptr;
    s_manual = false;
    s_fovDirty = false;
    s_fovCam = nullptr;
    s_hookDepth = 0;      // a stale depth would passthrough forever
    resetDispatchCounts();
    s_lastHookSeen = 0;
    s_lastOverrideSeen = 0;
    s_perfSelfTicks = 0;
    s_perfSelfMax = 0;
    s_perfEngineTicks = 0;
    s_perfEngineMax = 0;
    s_dbgEngine = -1;
    s_dbgMode = -1;
    s_lastDirMode = -1;
}

void DrawMenuItem()
{
    ImGui::Checkbox("Modern Camera", &s_enabled);
    if (!s_enabled)
        return;
    ImGui::Indent();
    ImGui::TextDisabled("Right stick steers; no auto-follow once touched.");
    ImGui::TextDisabled("R3 first person and cutscenes unchanged.");
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Sensitivity X##mcam", &s_sensX, kSensMin, kSensMax, "%.2f"))
        SetSensitivityX(s_sensX);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Sensitivity Y##mcam", &s_sensY, kSensMin, kSensMax, "%.2f"))
        SetSensitivityY(s_sensY);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("FOV scale##mcam", &s_fovScale, kFovScaleMin,
                           kFovScaleMax, "x%.2f"))
        SetFovScale(s_fovScale);
    ImGui::Checkbox("Global FOV##mcam", &s_globalFov);
    ImGui::SameLine();
    ImGui::TextDisabled("(all cameras, incl. first person)");
    ImGui::Checkbox("Modern camera on Epona##mcam", &s_horse);
    ImGui::Checkbox("Invert X##mcam", &s_invertX);
    ImGui::SameLine();
    ImGui::Checkbox("Invert Y##mcam", &s_invertY);
    // Field-verifiable diagnostics. "eng" is the engine index the dispatcher
    // actually ran last (1 = chase, 4 = first person, 8 = ride); "mode" is
    // the camera mode at that dispatch (0 = normal follow -- the only mode
    // the override runs in; 1 = LockOn recenter, 2 = target lock, 7 =
    // charge, 8 = riding lock). If "hook" doesn't climb during play the
    // dispatch never reaches us; if active=0 one of the gates (mode, or the
    // evt/camplay bytes shown) is blocking; if the stick stays 0.00 while
    // pushed, the pad-cache offset is wrong.
    ImGui::TextDisabled("hook: %u  eng: %d  mode: %d  active: %d  manual: %d  gear: %d",
                        (unsigned)loadHookRuns(), s_dbgEngine, s_dbgMode,
                        (int)s_dbgActive, (int)s_manual, s_dbgGear);
    ImGui::TextDisabled("stick: %.2f, %.2f  evt: %u  camplay: %u",
                        s_dbgStickX, s_dbgStickY,
                        (unsigned)*(volatile u8*)GAME_ADDR_evtStatus,
                        (unsigned)*(volatile u8*)GAME_ADDR_decoupleFlag);
    // Dispatch cost in microseconds, last/max: "self" is the hook's own
    // pre+post work, "eng" the game's engine underneath it. Self should stay
    // in single-digit us; a large eng max under the override but not vanilla
    // would mean the rebuilt eye is making the caller's wall check work
    // harder. Click to reset the maxima.
    const float toUs = 1.0e6f / (float)OSTimerClockSpeed;
    ImGui::TextDisabled("cost us: self %.1f (max %.1f)  eng %.1f (max %.1f)",
                        (float)s_perfSelfTicks * toUs,
                        (float)s_perfSelfMax * toUs,
                        (float)s_perfEngineTicks * toUs,
                        (float)s_perfEngineMax * toUs);
    if (ImGui::IsItemClicked()) {
        s_perfSelfMax = 0;
        s_perfEngineMax = 0;
    }
    ImGui::Unindent();
}

} // namespace ModernCamera
} // namespace Tools
