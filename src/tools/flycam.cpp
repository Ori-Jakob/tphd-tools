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
static bool  s_initialized = false; // direction state seeded
static u32   s_prevButtons = 0;
static u32   s_inputLock = 0;       // activation buttons ignored until released
static float s_speed = kSpeedInit;
static float s_hcosYaw = 0, s_hsinYaw = 0, s_sinPitch = 0, s_cosPitch = 0;
static cXyz  s_savedAt = {}, s_savedEye = {};

void DrawMenuItem()     { ImGui::Checkbox("Fly Cam", &s_enabled); }
bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }
bool IsActive()         { return s_active; }

// GamePad -> Pro button normalization (so the logic is device-neutral), matching
// fc_normalize_buttons in the cheat. Pro input is already in the target codes.
static u32 normalizeButtons(u32 g, u8 type)
{
    if (type == 1)
        return g;
    u32 p = 0;
    if (g & 0x00008000) p |= 0x00000010;  // A
    if (g & 0x00004000) p |= 0x00000040;  // B
    if (g & 0x00002000) p |= 0x00000008;  // X
    if (g & 0x00001000) p |= 0x00000020;  // Y
    if (g & 0x00000008) p |= 0x00000400;  // +
    if (g & 0x00000004) p |= 0x00001000;  // -
    if (g & 0x00040000) p |= 0x00020000;  // L3
    if (g & 0x00020000) p |= 0x00010000;  // R3
    if (g & 0x00000020) p |= 0x00002000;  // L
    if (g & 0x00000080) p |= 0x00000080;  // ZL
    if (g & 0x00000010) p |= 0x00000200;  // R
    if (g & 0x00000040) p |= 0x00000004;  // ZR
    if (g & 0x00000200) p |= 0x00000001;  // D-Up
    if (g & 0x00000100) p |= 0x00004000;  // D-Down
    if (g & 0x00000800) p |= 0x00000002;  // D-Left
    if (g & 0x00000400) p |= 0x00008000;  // D-Right
    return p;
}

static void deactivate()
{
    s_active = false;
    s_initialized = false;
    s_inputLock = 0;
    dCam_setFreeze(false);
    dCam_setDecouple(false);
}

// Seed yaw/pitch direction state from the camera's current at - eye.
static void initDirection()
{
    dCam_setDecouple(true);
    CameraXform* cam = dCam_getXform();
    if (!cam)
        return;
    float dx = cam->at.x - cam->eye.x;
    float dy = cam->at.y - cam->eye.y;
    float dz = cam->at.z - cam->eye.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) { dx /= len; dy /= len; dz /= len; }

    s_sinPitch = dy;
    float cp = sqrtf(dx * dx + dz * dz);
    s_cosPitch = cp;
    if (cp != 0.0f) { s_hcosYaw = dx / cp; s_hsinYaw = dz / cp; }
    else            { s_hcosYaw = 1.0f;    s_hsinYaw = 0.0f; }
    s_initialized = true;
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

    CameraXform* cam = dCam_getXform();
    if (!cam)
        return;
    cam->eye.x += dx; cam->eye.y += dy; cam->eye.z += dz;
    cam->at.x = cam->eye.x + s_hcosYaw * s_cosPitch * kTargetDist;
    cam->at.y = cam->eye.y + s_sinPitch * kTargetDist;
    cam->at.z = cam->eye.z + s_hsinYaw * s_cosPitch * kTargetDist;

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
}

void Tick()
{
    if (!s_enabled) {
        if (s_active)
            deactivate();
        return;
    }

    GameInput in;
    if (!dCam_getInput(&in))
        return;
    u32 buttons = normalizeButtons(in.buttons, in.type);

    // Ignore activation buttons until each is released.
    s_inputLock &= buttons;
    buttons &= ~s_inputLock;

    u32 newly = buttons & ~s_prevButtons;   // rising edges
    s_prevButtons = buttons;

    if (!s_active) {
        // Activate on ZL+ZR+L3+R3 with at least one fresh press.
        if ((buttons & PRO_COMBO) == PRO_COMBO && (newly & PRO_COMBO) != 0) {
            CameraXform* cam = dCam_getXform();
            if (!cam)
                return;
            s_savedAt  = cam->at;
            s_savedEye = cam->eye;
            s_inputLock = PRO_COMBO;   // wait for release before in-mode buttons act
            s_active = true;
            s_initialized = false;
        }
        return;
    }

    // --- active ---
    dCam_setFreeze(true);   // re-assert each frame

    if (newly & PRO_BTN_L3) {           // exit: restore the original camera
        CameraXform* cam = dCam_getXform();
        if (cam) { cam->at = s_savedAt; cam->eye = s_savedEye; }
        deactivate();
        return;
    }
    if (newly & PRO_BTN_R3) {           // exit: teleport Link to the camera
        CameraXform* cam = dCam_getXform();
        cXyz* link = dCam_getLinkPos();
        if (cam && link)
            *link = cam->eye;
        deactivate();
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

    if (!s_initialized)
        initDirection();
    move(buttons, in);
}

} // namespace FlyCam
} // namespace Tools
