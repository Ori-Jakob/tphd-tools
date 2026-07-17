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
// the flick that steps it can fire before the dispatch. Z-target lock-on
// often keeps the chase engine running with the attention-lock flag set, so
// owned engines pass through untouched while that flag is up.
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
static bool  s_fovDirty = false;   // viewcache fovy currently holds a scaled value
static float s_fovUnscaled = 0.0f;
static uint32_t s_hookRuns = 0;      // any engine dispatch seen
static uint32_t s_overrideRuns = 0;  // dispatches where the direction was owned

static dCamEngine_t s_realEngine[DCAM_ENGINE_COUNT] = {};
static bool s_hookInstalled = false;

// Menu diagnostics, captured in the wrappers so the readout shows what the
// override actually saw (not what the present thread re-derives).
static bool  s_dbgActive = false;
static float s_dbgStickX = 0.0f;
static float s_dbgStickY = 0.0f;
static int   s_dbgGear = 0;
static int   s_dbgEngine = -1;     // last engine index actually dispatched

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

    u8* cam = (u8*)camera;
    // Give the engine back the unscaled FOV it expects to lerp from.
    if (s_fovDirty) {
        *(float*)(cam + DCAM_OFF_VIEW_FOVY) = s_fovUnscaled;
        s_fovDirty = false;
    }

    const bool wantsDirection =
        idx == DCAM_ALG_CHASE || (idx == DCAM_ALG_RIDE && s_horse);
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool active = false;
    if (wantsDirection) {
        // Z-target lock-on often keeps these engines running with the
        // attention-lock flag set; the camera must stay fixed on the target
        // like vanilla, so pass the frame through untouched (and drop the
        // latch -- releasing the target reseeds from the lock-on framing).
        active = overrideActive() && !dCam_isAttentionLocked(camera);
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
            for (u32 i = 0; i < DCAM_PAD_CSTICK_WINDOW; ++i)
                cam[DCAM_OFF_PAD_CSTICK + i] = 0;

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

    int result = real(camera, style);
    ++s_hookRuns;
    s_dbgEngine = idx;

    if (active) {
        ++s_overrideRuns;
        applyOverride(cam, stickX, stickY);
    } else if (s_enabled && s_globalFov) {
        // Global FOV: keep the scale under first person, lock-on, talk,
        // cutscenes -- every camera.
        applyFovScale(cam);
    }
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
}

void Tick()
{
    if (s_enabled)
        installHooks();
    else if (s_hookInstalled)
        removeHooks();

    // dusklight resets its manual latch at the dispatch site whenever a
    // non-owned engine is selected. Our equivalent signal: if no owned
    // dispatch happened since the last present, something else (first
    // person, lock-on, cutscene, ...) ran the camera, so the next owned
    // frame must start vanilla. Deliberately NOT derived from
    // dCam_getAlgorithm(): mCurrentStyle can lag the style the dispatcher
    // actually used, and a false mismatch here would clear the latch every
    // frame -- turning the override into exactly the rubber-band behavior
    // it exists to remove.
    static uint32_t s_lastSeenOverrides = 0;
    if (s_hookInstalled && s_overrideRuns == s_lastSeenOverrides)
        s_manual = false;
    s_lastSeenOverrides = s_overrideRuns;
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
    s_hookRuns = 0;
    s_overrideRuns = 0;
    s_dbgEngine = -1;
}

void DrawMenuItem()
{
    ImGui::Checkbox("Modern Camera", &s_enabled);
    if (!s_enabled)
        return;
    ImGui::Indent();
    ImGui::TextDisabled("Right stick steers; no auto-follow once touched.");
    ImGui::TextDisabled("R3 first person, lock-on and cutscenes unchanged.");
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
    // actually ran last (1 = chase, 4 = first person, 8 = ride); if "hook"
    // doesn't climb during play the dispatch never reaches us; if active=0
    // one of the gates (evt/camplay bytes shown) is blocking; if the stick
    // stays 0.00 while pushed, the pad-cache offset is wrong.
    ImGui::TextDisabled("hook: %u  eng: %d  active: %d  manual: %d  gear: %d",
                        (unsigned)s_hookRuns, s_dbgEngine, (int)s_dbgActive,
                        (int)s_manual, s_dbgGear);
    ImGui::TextDisabled("stick: %.2f, %.2f  evt: %u  camplay: %u",
                        s_dbgStickX, s_dbgStickY,
                        (unsigned)*(volatile u8*)GAME_ADDR_evtStatus,
                        (unsigned)*(volatile u8*)GAME_ADDR_decoupleFlag);
    ImGui::Unindent();
}

} // namespace ModernCamera
} // namespace Tools
