// tools/flycam.cpp -- see flycam.h.
//
// A faithful C++ port of the shipped FlyCam graphics-pack cheat
// (TwilightPrincessHD/Cheats/FlyCam/patch_flycam.asm), running from our present
// hook instead of an in-game codecave. Controls:
//   ZL+ZR+L3+R3  toggle fly mode (each held button must be released first)
//   Left stick   move (relative to facing)      Right stick  rotate (yaw/pitch)
//   R / L        speed up / down (x2 / x0.5)     ZR / ZL      up / down
//   Hold A       max speed         Hold B        min speed   (NEW vs the cheat)
//   L3           exit, camera returns to Link
//   R3           exit, Link teleports to the camera
#include "tools/flycam.h"

#include "imgui.h"
#include "overlay.h"     // ov::g_settings.flyCamCombo, MenuButtonsToPro
#include "game/game.h"   // d_camera.h accessors

#include <math.h>

namespace Tools {
namespace FlyCam {

// ---- tuning (matches the cheat) ---------------------------------------------
static const float kSpeedInit = 20.0f;
static const float kSpeedMul  = 2.0f;
static const float kSpeedMax  = 200.0f;
static const float kSpeedMin  = 5.0f;
static const float kRotSpeed  = 0.08f;
static const float kTargetDist = 4.0f;
static const float kPitchClamp = 0.98f;
static const float kCosPitchLim = 0.199f;   // sqrt(1 - 0.98^2)

// ---- state ------------------------------------------------------------------
static bool  s_enabled = false;     // feature on/off (checkbox)
static bool  s_active  = false;     // currently flying
static u32   s_prevButtons = 0;
static u32   s_inputLock = 0;       // activation buttons ignored until released
static float s_speed = kSpeedInit;
static float s_hcosYaw = 0, s_hsinYaw = 0, s_sinPitch = 0, s_cosPitch = 0;
static CameraXform s_savedView = {};
static CameraXform s_flyView = {};
static void deactivate(const CameraXform* handoffView);

void DrawMenuItem()
{
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Fly Cam", &enabled))
        SetEnabled(enabled);
}
bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }
bool IsActive()         { return s_active; }
void Stop()
{
    if (s_active)
        deactivate(&s_flyView);
}

void OnApplicationStart()
{
    // Aroma plugin statics survive process changes; game-owned flags do not.
    s_active = false;
    s_prevButtons = 0;
    s_inputLock = 0;
    s_speed = kSpeedInit;
}

// Pro-button mask for the configured activation combo (0 if unbound).
static u32 activationCombo()
{
    return (u32)ov::MenuButtonsToPro(ov::g_settings.flyCamCombo);
}

// End decoupled ownership only after synchronizing the chosen handoff view
// through dCamera_c::Reset(). This prevents the normal camera from resuming
// with a stale current view, desired cache, direction, or HD transition state.
static void deactivate(const CameraXform* handoffView)
{
    if (handoffView)
        dCam_snapXform(&handoffView->at, &handoffView->eye);
    s_active = false;
    s_inputLock = 0;
    dCam_setFreeze(false);
    dCam_setDecouple(false);
}

// Seed yaw/pitch direction state from a coherent target/eye pair.
static void initDirection(const CameraXform& view)
{
    float dx = view.at.x - view.eye.x;
    float dy = view.at.y - view.eye.y;
    float dz = view.at.z - view.eye.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) { dx /= len; dy /= len; dz /= len; }

    s_sinPitch = dy;
    float cp = sqrtf(dx * dx + dz * dz);
    s_cosPitch = cp;
    if (cp != 0.0f) { s_hcosYaw = dx / cp; s_hsinYaw = dz / cp; }
    else            { s_hcosYaw = 1.0f;    s_hsinYaw = 0.0f; }
}

// While decoupled, the game still commits mViewCache to the final/rendered
// camera each frame. Fly Cam therefore owns only that desired cache while it
// is active; native Reset() is reserved for activation and handoff boundaries.
static bool publishFlyView()
{
    CameraXform* desired = dCam_getDesiredXform();
    if (!desired)
        return false;
    *desired = s_flyView;
    return true;
}

static bool activate(u32 combo)
{
    CameraXform* current = dCam_getCurrentXform();
    if (!current)
        return false;

    s_savedView = *current;
    s_flyView = *current;
    initDirection(s_flyView);

    dCam_setFreeze(true);
    dCam_setDecouple(true);
    if (!dCam_snapXform(&s_flyView.at, &s_flyView.eye)) {
        dCam_setDecouple(false);
        dCam_setFreeze(false);
        return false;
    }

    s_inputLock = combo;   // wait for release before in-mode buttons act
    s_active = true;
    return true;
}

static void move(u32 buttons, const GameInput& in)
{
    // Game reports L/R stick X inverted; the mirror flag flips back.
    float lx = -in.leftX, ly = in.leftY;
    float rx = -in.rightX, ry = in.rightY;
    if (dCam_getMirror()) { lx = -lx; rx = -rx; }

    float vert = 0.0f;
    if (buttons & PRO_BTN_ZR)      vert = 1.0f;
    else if (buttons & PRO_BTN_ZL) vert = -1.0f;

    // A = max speed while held, B = min speed while held (new).
    float spd = s_speed;
    if (buttons & PRO_BTN_A)      spd = kSpeedMax;
    else if (buttons & PRO_BTN_B) spd = kSpeedMin;

    float dx = ly * s_hcosYaw * s_cosPitch - lx * s_hsinYaw;
    float dz = ly * s_hsinYaw * s_cosPitch + lx * s_hcosYaw;
    float dy = ly * s_sinPitch + vert;
    dx *= spd; dy *= spd; dz *= spd;

    s_flyView.eye.x += dx;
    s_flyView.eye.y += dy;
    s_flyView.eye.z += dz;
    s_flyView.at.x =
        s_flyView.eye.x + s_hcosYaw * s_cosPitch * kTargetDist;
    s_flyView.at.y = s_flyView.eye.y + s_sinPitch * kTargetDist;
    s_flyView.at.z =
        s_flyView.eye.z + s_hsinYaw * s_cosPitch * kTargetDist;

    // Yaw from right stick X.
    float dyaw = rx * kRotSpeed;
    float nhc = s_hcosYaw - dyaw * s_hsinYaw;
    float nhs = s_hsinYaw + dyaw * s_hcosYaw;
    float ny = sqrtf(nhc * nhc + nhs * nhs);
    if (ny > 0.0f) { nhc /= ny; nhs /= ny; }

    // Pitch from right stick Y.
    float dpit = ry * kRotSpeed;
    float nsp = s_sinPitch + dpit * s_cosPitch;
    float ncp = s_cosPitch - dpit * s_sinPitch;
    float np = sqrtf(nsp * nsp + ncp * ncp);
    if (np > 0.0f) { nsp /= np; ncp /= np; }

    if (nsp > kPitchClamp)       { nsp = kPitchClamp;  ncp = kCosPitchLim; }
    else if (nsp < -kPitchClamp) { nsp = -kPitchClamp; ncp = kCosPitchLim; }

    s_hcosYaw = nhc; s_hsinYaw = nhs; s_sinPitch = nsp; s_cosPitch = ncp;
    publishFlyView();
}

void Tick()
{
    if (!s_enabled) {
        if (s_active)
            deactivate(&s_flyView);
        return;
    }

    // These flags are shared game state, so reassert ownership before reading
    // input. A transient controller-read failure must not let the engine run a
    // normal-camera frame through the middle of a flight.
    if (s_active) {
        dCam_setFreeze(true);
        dCam_setDecouple(true);
        publishFlyView();
    }

    GameInput in;
    if (!dCam_getInput(&in))
        return;
    u32 buttons = dCam_normalizeButtons(in.buttons, in.type);

    // Ignore activation buttons until each is released.
    s_inputLock &= buttons;
    buttons &= ~s_inputLock;

    u32 newly = buttons & ~s_prevButtons;   // rising edges
    s_prevButtons = buttons;

    if (!s_active) {
        // Activate on the rebindable combo with at least one fresh press.
        u32 combo = activationCombo();
        if (combo != 0 && (buttons & combo) == combo && (newly & combo) != 0) {
            activate(combo);
        }
        return;
    }

    // --- active ---
    if (newly & PRO_BTN_L3) {           // exit: restore the original camera
        deactivate(&s_savedView);
        return;
    }
    if (newly & PRO_BTN_R3) {           // exit: teleport Link to the camera
        cXyz* link = dCam_getLinkPos();
        if (link)
            *link = s_flyView.eye;
        deactivate(&s_flyView);
        return;
    }
    if (newly & PRO_BTN_R) {            // speed up
        s_speed *= kSpeedMul;
        if (s_speed > kSpeedMax) s_speed = kSpeedMax;
    }
    if (newly & PRO_BTN_L) {            // speed down
        s_speed *= 0.5f;
        if (s_speed < kSpeedMin) s_speed = kSpeedMin;
    }

    move(buttons, in);
}

} // namespace FlyCam
} // namespace Tools
