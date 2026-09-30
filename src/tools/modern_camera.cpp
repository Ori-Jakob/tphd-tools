#include "tools/modern_camera.h"

#include "core/config.h"
#include "core/logger.h"

#include "d/d_camera.h"
#include "d/d_event.h"

#include <coreinit/cache.h>
#include <math.h>
#include <stdint.h>

namespace Tools {
namespace ModernCamera {
static const float kDegPerFrame   = 5.0f;
static const float kPitchRatio    = 0.7f;
static const float kInclMinDeg    = -30.0f;
static const float kInclMaxDeg    = 50.0f;
static const float kStickDeadzone = 0.10f;
static const float kSensMin       = 0.2f;
static const float kSensMax       = 3.0f;
static const float kFovScaleMin   = 0.60f;
static const float kFovScaleMax   = 1.50f;
static const float kFovyMin       = 20.0f;
static const float kFovyMax       = 120.0f;
static const float kDegToRad      = 0.01745329252f;
static const float kDegToAngle    = 65536.0f / 360.0f;
static const float kAngleToDeg    = 360.0f / 65536.0f;

static bool  s_enabled = false;
static float s_sensX = 1.0f;
static float s_sensY = 1.0f;
static float s_fovScale = 1.0f;
static bool  s_globalFov = false;
static bool  s_horse = false;
static bool  s_invertX = false;
static bool  s_invertY = false;

static bool  s_acceptInput = false;
static bool  s_manual = false;
static float s_yawDeg = 0.0f;
static float s_pitchDeg = 0.0f;
static float s_sinYaw = 0.0f;
static float s_cosYaw = 1.0f;
static float s_sinPitch = 0.0f;
static float s_cosPitch = 1.0f;
static s16   s_viewAzimuth = 0;
static s16   s_viewInclination = 0;
static bool  s_directionDirty = true;
static int   s_lastDirMode = -1;

static bool       s_fovDirty = false;
static float      s_fovUnscaled = 0.0f;
static dCamera_c* s_fovCam = nullptr;

static int  s_hookDepth = 0;
static bool s_sawDispatch = false;
static bool s_sawOwned = false;

static dCamEngine_t s_realEngine[dCam_ENGINE_COUNT];
static bool         s_hookInstalled = false;
static bool         s_installWarned = false;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool  IsEnabled()                 { return s_enabled; }
float GetSensitivityX()           { return s_sensX; }
float GetSensitivityY()           { return s_sensY; }
float GetFovScale()               { return s_fovScale; }
bool  IsGlobalFovEnabled()        { return s_globalFov; }
void  SetGlobalFovEnabled(bool e) { s_globalFov = e; }
bool  IsHorseEnabled()            { return s_horse; }
void  SetHorseEnabled(bool e)     { s_horse = e; }
bool  IsInvertXEnabled()          { return s_invertX; }
void  SetInvertXEnabled(bool e)   { s_invertX = e; }
bool  IsInvertYEnabled()          { return s_invertY; }
void  SetInvertYEnabled(bool e)   { s_invertY = e; }

void SetSensitivityX(float s) { s_sensX = clampf(s, kSensMin, kSensMax); }
void SetSensitivityY(float s) { s_sensY = clampf(s, kSensMin, kSensMax); }
void SetFovScale(float s)     { s_fovScale = clampf(s, kFovScaleMin, kFovScaleMax); }

void SetEnabled(bool e)
{
    if (!e)
        s_manual = false;
    s_enabled = e;
}

static bool overrideAllowed()
{
    return s_enabled && !dEvt_isEventRunning() && !dEvent_isCameraPlay();
}

static void applyFovScale(dCamera_c* cam)
{
    if (s_fovScale == 1.0f)
        return;
    s_fovUnscaled = cam->mFovy;
    s_fovCam = cam;
    cam->mFovy = clampf(s_fovUnscaled * s_fovScale, kFovyMin, kFovyMax);
    s_fovDirty = true;
}

static void updateDirectionCache()
{
    s_viewAzimuth = (s16)(int)(s_yawDeg * kDegToAngle);
    s_viewInclination = (s16)(int)(s_pitchDeg * kDegToAngle);
    s_sinPitch = sinf(s_pitchDeg * kDegToRad);
    s_cosPitch = cosf(s_pitchDeg * kDegToRad);
    s_sinYaw = sinf(s_yawDeg * kDegToRad);
    s_cosYaw = cosf(s_yawDeg * kDegToRad);
    s_directionDirty = false;
}

static void steer(float stickX, float stickY)
{
    float rx = stickX;
    float ry = stickY;
    if (rx > -kStickDeadzone && rx < kStickDeadzone)
        rx = 0.0f;
    if (ry > -kStickDeadzone && ry < kStickDeadzone)
        ry = 0.0f;
    if (rx == 0.0f && ry == 0.0f)
        return;

    s_manual = true;
    s_yawDeg += (s_invertX ? -rx : rx) * kDegPerFrame * s_sensX;
    s_pitchDeg += (s_invertY ? ry : -ry) * kDegPerFrame * kPitchRatio * s_sensY;
    if (s_yawDeg > 180.0f)
        s_yawDeg -= 360.0f;
    else if (s_yawDeg < -180.0f)
        s_yawDeg += 360.0f;
    s_pitchDeg = clampf(s_pitchDeg, kInclMinDeg, kInclMaxDeg);
    s_directionDirty = true;
}

static void applyOverride(dCamera_c* cam, float stickX, float stickY)
{
    applyFovScale(cam);

    if (!s_manual) {
        s_yawDeg = (float)cam->mViewDir.mAzimuth * kAngleToDeg;
        s_pitchDeg = (float)cam->mViewDir.mInclination * kAngleToDeg;
    }
    if (s_acceptInput)
        steer(stickX, stickY);
    if (!s_manual)
        return;

    if (s_directionDirty)
        updateDirectionCache();
    cam->mViewDir.mAzimuth = s_viewAzimuth;
    cam->mViewDir.mInclination = s_viewInclination;

    const float r = cam->mViewDir.mRadius;
    const float horizontal = r * s_cosPitch;
    const cXyz& center = cam->mViewCache.mAt;
    cXyz& eye = cam->mViewCache.mEye;
    eye.x = center.x + horizontal * s_sinYaw;
    eye.y = center.y + r * s_sinPitch;
    eye.z = center.z + horizontal * s_cosYaw;
}

static bool ownsMode(int mode)
{
    return mode == dCam_MODE_FOLLOW || mode == dCam_MODE_RESET ||
           mode == dCam_MODE_LOCK || mode == dCam_MODE_RIDE_LOCK;
}

static int dispatch(int idx, dCamera_c* cam, int style)
{
    const dCamEngine_t real = s_realEngine[idx];
    if (!real)
        return 0;

    // Nested engine calls must not scale the FOV a second time.
    if (s_hookDepth > 0) {
        ++s_hookDepth;
        const int nested = real(cam, style);
        --s_hookDepth;
        return nested;
    }
    ++s_hookDepth;

    // Engines lerp the FOV from their own previous value, so hand back the unscaled one.
    if (s_fovDirty) {
        if (cam == s_fovCam)
            cam->mFovy = s_fovUnscaled;
        s_fovDirty = false;
    }

    const bool wantsDirection = idx == dCam_ALG_CHASE || (idx == dCam_ALG_RIDE && s_horse);
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool owned = false;
    if (wantsDirection) {
        const int mode = cam->mCurMode;
        if (mode != s_lastDirMode) {
            s_manual = false;
            s_lastDirMode = mode;
        }
        owned = overrideAllowed() && ownsMode(mode);
        if (owned) {
            stickX = cam->mCStick.mLastX;
            stickY = cam->mCStick.mLastY;
            // The engine's own right-stick turn and flick-zoom would fight the override.
            volatile u32* window = (volatile u32*)&cam->mCStick;
            for (u32 i = 0; i < dCamera_CSTICK_WINDOW / 4; ++i)
                window[i] = 0;
            cam->mGear = 0;
        } else {
            s_manual = false;
        }
    }

    const int result = real(cam, style);

    s_sawDispatch = true;
    if (owned) {
        s_sawOwned = true;
        applyOverride(cam, stickX, stickY);
    } else if (s_enabled && s_globalFov) {
        applyFovScale(cam);
    }
    --s_hookDepth;
    return result;
}

template <int N>
static int engineHook(dCamera_c* cam, int style)
{
    return dispatch(N, cam, style);
}

static const dCamEngine_t kHooks[dCam_ENGINE_COUNT] = {
    engineHook<0>,  engineHook<1>,  engineHook<2>,  engineHook<3>,
    engineHook<4>,  engineHook<5>,  engineHook<6>,  engineHook<7>,
    engineHook<8>,  engineHook<9>,  engineHook<10>, engineHook<11>,
    engineHook<12>, engineHook<13>, engineHook<14>, engineHook<15>,
    engineHook<16>, engineHook<17>, engineHook<18>, engineHook<19>,
};

static void flushTable()
{
    DCFlushRange((void*)dCam_getEngineSlot(0), dCam_ENGINE_COUNT * dCam_ENGINE_ENTRY_STRIDE);
    OSMemoryBarrier();
}

static bool installHooks()
{
    for (int i = 0; i < dCam_ENGINE_COUNT; ++i) {
        if (!*dCam_getEngineSlot(i)) {
            if (!s_installWarned) {
                s_installWarned = true;
                Logger::LogWarn("[mcam] camera table slot %d is empty, waiting", i);
            }
            return false;
        }
    }
    for (int i = 0; i < dCam_ENGINE_COUNT; ++i) {
        volatile dCamEngine_t* slot = dCam_getEngineSlot(i);
        const dCamEngine_t current = *slot;
        if (current != kHooks[i])
            s_realEngine[i] = current;
        *slot = kHooks[i];
    }
    flushTable();
    s_hookInstalled = true;
    s_sawDispatch = false;
    s_sawOwned = false;
    Logger::Log("[mcam] camera table hooked");
    return true;
}

static void removeHooks()
{
    if (!s_hookInstalled)
        return;
    for (int i = 0; i < dCam_ENGINE_COUNT; ++i) {
        volatile dCamEngine_t* slot = dCam_getEngineSlot(i);
        if (*slot == kHooks[i] && s_realEngine[i])
            *slot = s_realEngine[i];
    }
    flushTable();
    s_hookInstalled = false;
    s_manual = false;
    s_fovDirty = false;
    s_fovCam = nullptr;
    s_sawDispatch = false;
    s_sawOwned = false;
    Logger::Log("[mcam] camera table restored");
}

void Tick(bool acceptInput)
{
    s_acceptInput = acceptInput;
    if (!s_enabled) {
        removeHooks();
        return;
    }
    if (!s_hookInstalled && !installHooks())
        return;

    // After a frame run only by other cameras, start again from the game's own direction.
    if (s_sawDispatch && !s_sawOwned)
        s_manual = false;
    s_sawDispatch = false;
    s_sawOwned = false;
}

void ResetToDefaults()
{
    SetEnabled(false);
    s_sensX = 1.0f;
    s_sensY = 1.0f;
    s_fovScale = 1.0f;
    s_globalFov = false;
    s_horse = false;
    s_invertX = false;
    s_invertY = false;
}

void OnApplicationStart()
{
    s_hookInstalled = false;
    s_installWarned = false;
    for (int i = 0; i < dCam_ENGINE_COUNT; ++i)
        s_realEngine[i] = nullptr;
    s_acceptInput = false;
    s_manual = false;
    s_directionDirty = true;
    s_lastDirMode = -1;
    s_fovDirty = false;
    s_fovCam = nullptr;
    s_hookDepth = 0;
    s_sawDispatch = false;
    s_sawOwned = false;
}

void OnApplicationEnd()
{
    removeHooks();
}

void LoadConfig(const cJSON* root)
{
    bool enabled = s_enabled;
    if (Config::ReadBool(root, "modernCameraEnabled", &enabled))
        SetEnabled(enabled);
    Config::ReadFloat(root, "modernCameraSensX", &s_sensX, kSensMin, kSensMax);
    Config::ReadFloat(root, "modernCameraSensY", &s_sensY, kSensMin, kSensMax);
    Config::ReadFloat(root, "modernCameraFovScale", &s_fovScale, kFovScaleMin, kFovScaleMax);
    Config::ReadBool(root, "modernCameraGlobalFov", &s_globalFov);
    Config::ReadBool(root, "modernCameraHorse", &s_horse);
    Config::ReadBool(root, "modernCameraInvertX", &s_invertX);
    Config::ReadBool(root, "modernCameraInvertY", &s_invertY);
}

void SaveConfig(cJSON* root)
{
    Config::AddBool(root, "modernCameraEnabled", s_enabled);
    Config::AddNumber(root, "modernCameraSensX", s_sensX);
    Config::AddNumber(root, "modernCameraSensY", s_sensY);
    Config::AddNumber(root, "modernCameraFovScale", s_fovScale);
    Config::AddBool(root, "modernCameraGlobalFov", s_globalFov);
    Config::AddBool(root, "modernCameraHorse", s_horse);
    Config::AddBool(root, "modernCameraInvertX", s_invertX);
    Config::AddBool(root, "modernCameraInvertY", s_invertY);
}
}
}
