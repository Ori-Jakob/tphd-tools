#include "tools/flycam.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"

#include "d/d_camera.h"
#include "d/d_event.h"

#include <math.h>
#include <stdint.h>

namespace Tools {
namespace FlyCam {
static const float kSpeedInit   = 20.0f;
static const float kSpeedStep   = 2.0f;
static const float kSpeedMax    = 200.0f;
static const float kSpeedMin    = 5.0f;
static const float kRotSpeed    = 0.08f;
static const float kTargetDist  = 4.0f;
static const float kPitchClamp  = 0.98f;
static const float kCosPitchLim = 0.199f;

static bool     s_enabled = false;
static bool     s_active = false;
static uint32_t s_inputLock = 0;
static float    s_speed = kSpeedInit;
static float    s_hcosYaw = 1.0f;
static float    s_hsinYaw = 0.0f;
static float    s_sinPitch = 0.0f;
static float    s_cosPitch = 1.0f;
static dCamera_xform_c s_savedView;
static dCamera_xform_c s_flyView;

bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }
bool IsActive()         { return s_active; }
bool WantsInputBlock()  { return s_active; }

static void releaseGame()
{
    dEvt_setEventRunning(false);
    dEvent_setCameraPlay(false);
}

static void leave(const dCamera_xform_c* handoff, const char* reason)
{
    if (handoff)
        dCam_snapXform(&handoff->mAt, &handoff->mEye);
    s_active = false;
    s_inputLock = 0;
    releaseGame();
    Hotkeys::SuppressUntilReleased(Input::Current().held);
    Input::DrainHeld();
    Logger::Log("[flycam] off: %s", reason);
}

void Stop()
{
    if (s_active)
        leave(&s_flyView, "stopped");
}

static void initDirection(const dCamera_xform_c& view)
{
    float dx = view.mAt.x - view.mEye.x;
    float dy = view.mAt.y - view.mEye.y;
    float dz = view.mAt.z - view.mEye.z;
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
        dz /= len;
    }

    s_sinPitch = dy;
    s_cosPitch = sqrtf(dx * dx + dz * dz);
    if (s_cosPitch != 0.0f) {
        s_hcosYaw = dx / s_cosPitch;
        s_hsinYaw = dz / s_cosPitch;
    } else {
        s_hcosYaw = 1.0f;
        s_hsinYaw = 0.0f;
    }
}

static void publishFlyView()
{
    if (dCamera_xform_c* desired = dCam_getDesiredXform())
        *desired = s_flyView;
}

static void activate()
{
    dCamera_xform_c* current = dCam_getCurrentXform();
    if (!current)
        return;

    s_savedView = *current;
    s_flyView = *current;
    initDirection(s_flyView);

    dEvt_setEventRunning(true);
    dEvent_setCameraPlay(true);
    if (!dCam_snapXform(&s_flyView.mAt, &s_flyView.mEye)) {
        releaseGame();
        return;
    }

    s_inputLock = Input::Current().held;
    Hotkeys::SuppressUntilReleased(s_inputLock);
    s_active = true;
    Logger::Log("[flycam] on at (%.0f %.0f %.0f)", (double)s_flyView.mEye.x,
                (double)s_flyView.mEye.y, (double)s_flyView.mEye.z);
}

static void move(uint32_t held, const Input::Snapshot& in)
{
    // Stick X runs against the game's yaw; a mirrored world flips it back.
    float lx = -in.lx, ly = in.ly;
    float rx = -in.rx, ry = in.ry;
    if (dCam_getMirror()) {
        lx = -lx;
        rx = -rx;
    }

    float vert = 0.0f;
    if (held & Input::BTN_ZR)
        vert = 1.0f;
    else if (held & Input::BTN_ZL)
        vert = -1.0f;

    float speed = s_speed;
    if (held & Input::BTN_A)
        speed = kSpeedMax;
    else if (held & Input::BTN_B)
        speed = kSpeedMin;

    const float dx = ly * s_hcosYaw * s_cosPitch - lx * s_hsinYaw;
    const float dz = ly * s_hsinYaw * s_cosPitch + lx * s_hcosYaw;
    const float dy = ly * s_sinPitch + vert;

    cXyz& eye = s_flyView.mEye;
    eye.x += dx * speed;
    eye.y += dy * speed;
    eye.z += dz * speed;
    s_flyView.mAt.x = eye.x + s_hcosYaw * s_cosPitch * kTargetDist;
    s_flyView.mAt.y = eye.y + s_sinPitch * kTargetDist;
    s_flyView.mAt.z = eye.z + s_hsinYaw * s_cosPitch * kTargetDist;

    const float dyaw = rx * kRotSpeed;
    float hc = s_hcosYaw - dyaw * s_hsinYaw;
    float hs = s_hsinYaw + dyaw * s_hcosYaw;
    const float yawLen = sqrtf(hc * hc + hs * hs);
    if (yawLen > 0.0f) {
        hc /= yawLen;
        hs /= yawLen;
    }

    const float dpitch = ry * kRotSpeed;
    float sp = s_sinPitch + dpitch * s_cosPitch;
    float cp = s_cosPitch - dpitch * s_sinPitch;
    const float pitchLen = sqrtf(sp * sp + cp * cp);
    if (pitchLen > 0.0f) {
        sp /= pitchLen;
        cp /= pitchLen;
    }
    if (sp > kPitchClamp) {
        sp = kPitchClamp;
        cp = kCosPitchLim;
    } else if (sp < -kPitchClamp) {
        sp = -kPitchClamp;
        cp = kCosPitchLim;
    }

    s_hcosYaw = hc;
    s_hsinYaw = hs;
    s_sinPitch = sp;
    s_cosPitch = cp;
    publishFlyView();
}

void Tick(bool acceptInput)
{
    if (!s_enabled) {
        if (s_active)
            leave(&s_flyView, "turned off");
        return;
    }

    if (s_active) {
        dEvt_setEventRunning(true);
        dEvent_setCameraPlay(true);
        publishFlyView();
    }

    const Input::Snapshot& in = Input::Current();
    if (!in.valid)
        return;
    s_inputLock &= in.held;
    if (!acceptInput)
        return;

    if (!s_active) {
        if (Hotkeys::Pressed(Hotkeys::HOTKEY_FLY_CAM))
            activate();
        return;
    }

    const uint32_t held = in.held & ~s_inputLock;
    const uint32_t pressed = in.pressed & ~s_inputLock;
    if (Hotkeys::Pressed(Hotkeys::HOTKEY_FLY_CAM) || (pressed & Input::BTN_L3)) {
        leave(&s_savedView, "back to Link");
        return;
    }
    if (pressed & Input::BTN_R3) {
        if (cXyz* link = dCam_getLinkPos())
            *link = s_flyView.mEye;
        leave(&s_flyView, "Link moved to the camera");
        return;
    }
    if (pressed & Input::BTN_R) {
        s_speed *= kSpeedStep;
        if (s_speed > kSpeedMax)
            s_speed = kSpeedMax;
    }
    if (pressed & Input::BTN_L) {
        s_speed /= kSpeedStep;
        if (s_speed < kSpeedMin)
            s_speed = kSpeedMin;
    }

    move(held, in);
}

void ResetToDefaults()
{
    s_enabled = false;
}

void OnApplicationStart()
{
    s_active = false;
    s_inputLock = 0;
    s_speed = kSpeedInit;
}

void OnApplicationEnd()
{
    if (s_active)
        releaseGame();
    s_active = false;
    s_inputLock = 0;
}

void LoadConfig(const cJSON* root)
{
    Config::ReadBool(root, "flyCamEnabled", &s_enabled);
}

void SaveConfig(cJSON* root)
{
    Config::AddBool(root, "flyCamEnabled", s_enabled);
}
}
}
