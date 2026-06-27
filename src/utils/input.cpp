// input.cpp -- see input.h.
#include "input.h"
#include "overlay.h"
#include "swkbd.h"

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiContext::NavWindowingTarget (window-picker state)

using namespace ov;

namespace Input {

// Buttons accumulated (OR'd) by the Filter* hooks during a frame, consumed at
// the next present. Touch comes from the GamePad only.
static uint32_t       s_accum     = 0;
static VPADTouchData  s_lastTouch = {};
static bool           s_haveTouch = false;

static uint32_t s_cur  = 0;   // this frame's neutral buttons (consumed stash)
static uint32_t s_prev = 0;   // last frame's, for edge detection
static bool     s_wasTouched = false;

static bool     s_hotkeyFired = false;
static bool     s_capturing   = false;
static bool     s_captureArmed = false;  // set once the controller is fully released
static uint32_t s_captureMask = 0;

static Snapshot s_pendingSnapshot = {};
static Snapshot s_snapshot = {};
static bool     s_pendingSnapshotActive = false;

// The hotkey edge is detected in the input hooks (which run before the game
// reads input), not at present -- so we can zero the game's input on the very
// frame the menu toggles (otherwise the hotkey combo leaks to the game that
// frame). The hook sets these; BeginFrame consumes + clears them at present.
static bool     s_togglePending = false;  // hotkey fired in a hook this frame
static bool     s_consumeFrame  = false;  // -> zero game input this (toggle) frame
static bool     s_gameResetPending = false; // reset combo fired in a hook this frame
static bool     s_gameResetFired   = false; // consumed by Overlay::Present
static bool     s_stateReloadArmed   = false;
static bool     s_stateReloadPending = false;
static bool     s_stateReloadFired   = false;

// After the hotkey fires (esp. on close), keep zeroing ALL game input until the
// controller is fully released -- otherwise the buttons still held when the menu
// closes leak straight into gameplay (e.g. the L+R you closed with becomes a
// game action). Cleared in BeginFrame once no buttons remain held.
static bool     s_drainHeld = false;

// Quick-transform takeover: strip Y from the game while ZR+Y is held so the item
// doesn't equip, and report the combo's rising edge to the cheat.
static bool     s_qtArmed = false;   // cheat enabled? (set by Cheats each frame)
static bool     s_qtHeld  = false;   // combo held this frame (set in the hooks)
static bool     s_qtPrev  = false;   // combo held last frame (for the edge)
static bool     s_qtFired = false;   // rising edge, consumed by the cheat
static const uint32_t kQtCombo = MB_ZR | MB_Y;

// Start maps to Plus in the device-neutral button set.
static const uint32_t kGameResetCombo = MB_PLUS | MB_X | MB_B;
static const uint32_t kStateReloadCombo = MB_ZL | MB_ZR | MB_PLUS;

static float magSq(float x, float y)
{
    return x * x + y * y;
}

static void mergeSnapshot(uint32_t source, uint32_t buttons,
                          float leftX, float leftY, float rightX, float rightY)
{
    bool active = buttons != 0 ||
                  magSq(leftX, leftY) > 0.0001f || magSq(rightX, rightY) > 0.0001f;
    s_pendingSnapshot.connected = true;
    if (active) {
        if (!s_pendingSnapshotActive) {
            s_pendingSnapshot.sourceMask = 0;
            s_pendingSnapshot.leftX = s_pendingSnapshot.leftY = 0.0f;
            s_pendingSnapshot.rightX = s_pendingSnapshot.rightY = 0.0f;
        }
        s_pendingSnapshotActive = true;
        s_pendingSnapshot.sourceMask |= source;
    } else if (!s_pendingSnapshotActive && s_pendingSnapshot.sourceMask == 0) {
        s_pendingSnapshot.sourceMask = source;
    }
    s_pendingSnapshot.buttons |= buttons;
    if (magSq(leftX, leftY) >= magSq(s_pendingSnapshot.leftX, s_pendingSnapshot.leftY)) {
        s_pendingSnapshot.leftX = leftX;
        s_pendingSnapshot.leftY = leftY;
    }
    if (magSq(rightX, rightY) >= magSq(s_pendingSnapshot.rightX, s_pendingSnapshot.rightY)) {
        s_pendingSnapshot.rightX = rightX;
        s_pendingSnapshot.rightY = rightY;
    }
}


// device -> neutral button mapping

static uint32_t mapVpad(uint32_t h)
{
    uint32_t m = 0;
    if (h & VPAD_BUTTON_UP)      m |= MB_UP;
    if (h & VPAD_BUTTON_DOWN)    m |= MB_DOWN;
    if (h & VPAD_BUTTON_LEFT)    m |= MB_LEFT;
    if (h & VPAD_BUTTON_RIGHT)   m |= MB_RIGHT;
    if (h & VPAD_BUTTON_A)       m |= MB_A;
    if (h & VPAD_BUTTON_B)       m |= MB_B;
    if (h & VPAD_BUTTON_X)       m |= MB_X;
    if (h & VPAD_BUTTON_Y)       m |= MB_Y;
    if (h & VPAD_BUTTON_L)       m |= MB_L;
    if (h & VPAD_BUTTON_R)       m |= MB_R;
    if (h & VPAD_BUTTON_ZL)      m |= MB_ZL;
    if (h & VPAD_BUTTON_ZR)      m |= MB_ZR;
    if (h & VPAD_BUTTON_PLUS)    m |= MB_PLUS;
    if (h & VPAD_BUTTON_MINUS)   m |= MB_MINUS;
    if (h & VPAD_BUTTON_STICK_L) m |= MB_LSTICK;
    if (h & VPAD_BUTTON_STICK_R) m |= MB_RSTICK;
    return m;
}

static uint32_t mapKpad(const KPADStatus& s)
{
    uint32_t m = 0;
    if (s.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        uint32_t h = s.pro.hold;
        if (h & WPAD_PRO_BUTTON_UP)      m |= MB_UP;
        if (h & WPAD_PRO_BUTTON_DOWN)    m |= MB_DOWN;
        if (h & WPAD_PRO_BUTTON_LEFT)    m |= MB_LEFT;
        if (h & WPAD_PRO_BUTTON_RIGHT)   m |= MB_RIGHT;
        if (h & WPAD_PRO_BUTTON_A)       m |= MB_A;
        if (h & WPAD_PRO_BUTTON_B)       m |= MB_B;
        if (h & WPAD_PRO_BUTTON_X)       m |= MB_X;
        if (h & WPAD_PRO_BUTTON_Y)       m |= MB_Y;
        if (h & WPAD_PRO_TRIGGER_L)      m |= MB_L;
        if (h & WPAD_PRO_TRIGGER_R)      m |= MB_R;
        if (h & WPAD_PRO_TRIGGER_ZL)     m |= MB_ZL;
        if (h & WPAD_PRO_TRIGGER_ZR)     m |= MB_ZR;
        if (h & WPAD_PRO_BUTTON_PLUS)    m |= MB_PLUS;
        if (h & WPAD_PRO_BUTTON_MINUS)   m |= MB_MINUS;
        if (h & WPAD_PRO_BUTTON_STICK_L) m |= MB_LSTICK;
        if (h & WPAD_PRO_BUTTON_STICK_R) m |= MB_RSTICK;
    } else if (s.extensionType == WPAD_EXT_CLASSIC) {
        uint32_t h = s.classic.hold;
        if (h & WPAD_CLASSIC_BUTTON_UP)    m |= MB_UP;
        if (h & WPAD_CLASSIC_BUTTON_DOWN)  m |= MB_DOWN;
        if (h & WPAD_CLASSIC_BUTTON_LEFT)  m |= MB_LEFT;
        if (h & WPAD_CLASSIC_BUTTON_RIGHT) m |= MB_RIGHT;
        if (h & WPAD_CLASSIC_BUTTON_A)     m |= MB_A;
        if (h & WPAD_CLASSIC_BUTTON_B)     m |= MB_B;
        if (h & WPAD_CLASSIC_BUTTON_L)     m |= MB_L;
        if (h & WPAD_CLASSIC_BUTTON_R)     m |= MB_R;
        if (h & WPAD_CLASSIC_BUTTON_ZL)    m |= MB_ZL;
        if (h & WPAD_CLASSIC_BUTTON_ZR)    m |= MB_ZR;
        if (h & WPAD_CLASSIC_BUTTON_PLUS)  m |= MB_PLUS;
        if (h & WPAD_CLASSIC_BUTTON_MINUS) m |= MB_MINUS;
    }
    return m;
}


// blocking decisions

static bool blockVpad()
{
    return g_menuVisible && g_settings.blockEnabled &&
           (g_settings.blockMode == BLOCK_ALL || g_settings.blockMode == BLOCK_GAMEPAD);
}
static bool blockKpad()
{
    return g_menuVisible && g_settings.blockEnabled &&
           (g_settings.blockMode == BLOCK_ALL || g_settings.blockMode == BLOCK_PRO);
}

// Edge-detect the open/close hotkey from a single device's neutral bits, against
// the previous frame's combined buttons. Called from the input hooks. When it
// fires we both request a toggle (applied at present) and consume this frame's
// game input. Skipped while rebinding (those presses set the new combo instead).
//
// Strict match: the device's buttons must equal the hotkey EXACTLY (no extra
// buttons held), so e.g. L+R only toggles when nothing else is pressed.
static void detectHotkey(uint32_t neutral)
{
    if (s_capturing)
        return;
    uint32_t hk = g_settings.hotkey;
    if (hk != 0 && neutral == hk && s_prev != hk) {
        s_togglePending = true;
        s_consumeFrame  = true;
        s_drainHeld     = true;   // keep eating input until everything is let go
    }
}

static void detectGameResetHotkey(uint32_t neutral)
{
    if (s_capturing || !g_settings.gameResetHotkey)
        return;
    if (neutral == kGameResetCombo && s_prev != kGameResetCombo) {
        s_gameResetPending = true;
        s_consumeFrame     = true;
        s_drainHeld        = true;
    }
}

static void detectSaveStateReloadHotkey(uint32_t neutral)
{
    if (s_capturing || !s_stateReloadArmed)
        return;
    if (neutral == kStateReloadCombo && s_prev != kStateReloadCombo) {
        s_stateReloadPending = true;
        s_consumeFrame       = true;
        s_drainHeld          = true;
    }
}


// hooks: stash + neutralize

void FilterVpad(void* buffers, int count)
{
    VPADStatus* b = (VPADStatus*)buffers;
    if (!b)
        return;
    // Only touch the samples the caller actually handed us. The Cemu codecave
    // wrapper always requests 8; a real VPADRead returns the number it filled
    // (its return value) -- the buffer may be shorter than 8, so clamp.
    int n = count;
    if (n < 1)
        return;
    if (n > 8)
        n = 8;

    uint32_t bits = mapVpad(b[0].hold);
    s_accum    |= bits;
    s_lastTouch = b[0].tpNormal;
    s_haveTouch = true;
    mergeSnapshot(SOURCE_GAMEPAD, bits, b[0].leftStick.x, b[0].leftStick.y,
                  b[0].rightStick.x, b[0].rightStick.y);
    detectHotkey(bits);
    detectGameResetHotkey(bits);
    detectSaveStateReloadHotkey(bits);
    SwKbd::SetVpad(&b[0]);   // feed the system keyboard (before we zero it)

    // Quick transform: while ZR+Y is held (and not in the menu), strip Y from the
    // game so it can't equip the Y-item -- the transform takes precedence.
    if (s_qtArmed && !g_menuVisible && (bits & kQtCombo) == kQtCombo) {
        s_qtHeld = true;
        for (int i = 0; i < n; i++) {
            b[i].hold    &= ~VPAD_BUTTON_Y;
            b[i].trigger &= ~VPAD_BUTTON_Y;
            b[i].release &= ~VPAD_BUTTON_Y;
        }
    }

    if (!blockVpad() && !s_consumeFrame && !s_drainHeld)
        return;
    for (int i = 0; i < n; i++) {
        VPADStatus* s = &b[i];
        s->hold = 0; s->trigger = 0; s->release = 0;
        s->leftStick.x  = 0.0f; s->leftStick.y  = 0.0f;
        s->rightStick.x = 0.0f; s->rightStick.y = 0.0f;
        s->tpNormal.touched    = 0;
        s->tpFiltered1.touched = 0;
        s->tpFiltered2.touched = 0;
    }
}

void FilterKpad(void* buffers, int count)
{
    KPADStatus* b = (KPADStatus*)buffers;
    if (!b)
        return;

    uint32_t bits = 0;
    for (int i = 0; i < count && i < 4; i++) {
        uint32_t one = mapKpad(b[i]);
        bits |= one;
        if (b[i].extensionType == WPAD_EXT_PRO_CONTROLLER) {
            mergeSnapshot(SOURCE_PRO, one, b[i].pro.leftStick.x, b[i].pro.leftStick.y,
                          b[i].pro.rightStick.x, b[i].pro.rightStick.y);
        } else if (b[i].extensionType == WPAD_EXT_CLASSIC) {
            mergeSnapshot(SOURCE_CLASSIC, one, b[i].classic.leftStick.x,
                          b[i].classic.leftStick.y, b[i].classic.rightStick.x,
                          b[i].classic.rightStick.y);
        }
    }
    s_accum |= bits;
    detectHotkey(bits);
    detectGameResetHotkey(bits);
    detectSaveStateReloadHotkey(bits);
    SwKbd::SetKpad(b, count);   // feed the system keyboard (before we zero it)

    // Quick transform: strip Y (Pro + Classic) while ZR+Y is held (see FilterVpad).
    if (s_qtArmed && !g_menuVisible && (bits & kQtCombo) == kQtCombo) {
        s_qtHeld = true;
        for (int i = 0; i < count && i < 4; i++) {
            b[i].pro.hold        &= ~WPAD_PRO_BUTTON_Y;
            b[i].pro.trigger     &= ~WPAD_PRO_BUTTON_Y;
            b[i].pro.release     &= ~WPAD_PRO_BUTTON_Y;
            b[i].classic.hold    &= ~WPAD_CLASSIC_BUTTON_Y;
            b[i].classic.trigger &= ~WPAD_CLASSIC_BUTTON_Y;
            b[i].classic.release &= ~WPAD_CLASSIC_BUTTON_Y;
        }
    }

    if (!blockKpad() && !s_consumeFrame && !s_drainHeld)
        return;
    int nz = (count < 4) ? count : 4;   // only zero the samples we were given
    for (int i = 0; i < nz; i++) {
        KPADStatus* s = &b[i];
        s->hold = 0; s->trigger = 0; s->release = 0;
        s->pro.hold = 0; s->pro.trigger = 0; s->pro.release = 0;
        s->pro.leftStick.x  = 0.0f; s->pro.leftStick.y  = 0.0f;
        s->pro.rightStick.x = 0.0f; s->pro.rightStick.y = 0.0f;
        s->classic.hold = 0; s->classic.trigger = 0; s->classic.release = 0;
        s->classic.leftStick.x  = 0.0f; s->classic.leftStick.y  = 0.0f;
        s->classic.rightStick.x = 0.0f; s->classic.rightStick.y = 0.0f;
        s->nunchuk.hold = 0; s->nunchuk.trigger = 0; s->nunchuk.release = 0;
        s->nunchuk.stick.x = 0.0f; s->nunchuk.stick.y = 0.0f;
    }
}


// per-present consumption

void BeginFrame()
{
    s_cur   = s_accum;
    s_accum = 0;   // reset for next frame's hooks
    s_snapshot = s_pendingSnapshot;
    s_snapshot.buttons = s_cur;
    s_snapshot.pressed = s_cur & ~s_prev;
    s_pendingSnapshot = {};
    s_pendingSnapshotActive = false;

    if (s_capturing) {
        if (!s_captureArmed) {
            // Don't start listening until the controller is fully released --
            // otherwise the button used to press "Rebind" (A) is captured
            // immediately and committed the instant it's let go.
            if (s_cur == 0)
                s_captureArmed = true;
        } else {
            s_captureMask |= s_cur;
            // Commit when the user releases everything (>=1 button seen).
            if (s_cur == 0 && s_captureMask != 0) {
                g_settings.hotkey = s_captureMask;
                s_capturing = false;
            }
        }
    }

    // The hotkey edge was already detected in the input hooks this frame.
    s_hotkeyFired   = s_togglePending;
    s_gameResetFired = s_gameResetPending;
    s_stateReloadFired = s_stateReloadPending;
    s_togglePending = false;
    s_gameResetPending = false;
    s_stateReloadPending = false;
    s_consumeFrame  = false;   // clear for next frame's hooks

    // Keep draining game input until every button is released (so the combo held
    // at menu-close doesn't leak into gameplay). s_cur reflects the real buttons
    // even while we zero the game's copy, so this clears the moment they let go.
    if (s_drainHeld && s_cur == 0)
        s_drainHeld = false;

    // Quick-transform combo rising edge (the hooks set s_qtHeld this frame).
    s_qtFired = s_qtHeld && !s_qtPrev;
    s_qtPrev  = s_qtHeld;
    s_qtHeld  = false;

    s_prev = s_cur;
}

bool HotkeyToggled()
{
    return s_hotkeyFired;
}

bool GameResetHotkeyFired()
{
    return s_gameResetFired;
}

bool SaveStateReloadHotkeyFired()
{
    return s_stateReloadFired;
}

void GetSnapshot(Snapshot* out)
{
    if (out)
        *out = s_snapshot;
}

void SetQuickTransformArmed(bool armed)
{
    s_qtArmed = armed;
    if (!armed)
        s_qtPrev = s_qtHeld = s_qtFired = false;
}

bool QuickTransformFired()
{
    return s_qtFired;
}

void SetSaveStateReloadHotkeyArmed(bool armed)
{
    s_stateReloadArmed = armed;
    if (!armed)
        s_stateReloadPending = s_stateReloadFired = false;
}

void FeedMenu(ImGuiIO& io, float dispW, float dispH)
{
    // Only drive ImGui nav while the menu is open. When it's closed we still run
    // (feeding everything as released) so ImGui never sees a held button -- otherwise
    // a button like X (mapped to GamepadFaceLeft, ImGui's windowing key) would open
    // the gamepad window-picker against the passive HUDs while the menu is shut.
    // Also suppressed while rebinding, so the captured buttons don't drive the menu.
    const uint32_t b = (s_capturing || !ov::g_menuVisible) ? 0u : s_cur;

    // While ImGui's window-picker overlay is up (hold X), route D-pad up/down to
    // its list navigation (GamepadL1/R1 = focus prev/next). Outside the overlay,
    // D-pad keeps its normal nav role. NavWindowingTarget reflects last frame
    // (FeedMenu runs before NewFrame), which is fine for a held-button overlay.
    ImGuiContext* gc = ImGui::GetCurrentContext();
    bool windowing = gc && gc->NavWindowingTarget != nullptr;

    io.AddKeyEvent(ImGuiKey_GamepadDpadUp,    !windowing && (b & MB_UP) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown,  !windowing && (b & MB_DOWN) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,  (b & MB_LEFT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (b & MB_RIGHT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  (b & MB_A) != 0);   // activate
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (b & MB_B) != 0);   // cancel
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  (b & MB_X) != 0);   // window-picker / menu key
    // FaceUp == ImGuiKey_NavGamepadInput: the "Input" nav action that opens a text
    // field for editing (sets ImGuiActivateFlags_PreferInput, which raises
    // io.WantTextInput -> our swkbd). Plain Activate (A/FaceDown) only sets
    // PreferTweak, which does NOT open an InputText. We drive FaceUp from A as well
    // so pressing A on a textbox opens the keyboard (the user-expected behavior);
    // Y stays free for the menu's window cycle (Menu::Draw reads it from the
    // snapshot, not via ImGui nav).
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    (b & MB_A) != 0);
    // L/R are repurposed by the menu for tab cycling, so feed them released --
    // except while the picker overlay is up, where D-pad up/down drives them.
    io.AddKeyEvent(ImGuiKey_GamepadL1,        windowing && (b & MB_UP) != 0);    // picker: prev
    io.AddKeyEvent(ImGuiKey_GamepadR1,        windowing && (b & MB_DOWN) != 0);  // picker: next

    // GamePad touch -> mouse (Cemu delivers a left-click on the window as touch).
    // Only while the menu is open, for the same reason as the nav keys above.
    if (s_haveTouch && ov::g_menuVisible) {
        VPADTouchData t;
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &t, &s_lastTouch);
        if (t.touched) {
            float sx = dispW / 1280.0f;
            float sy = dispH / 720.0f;
            io.AddMousePosEvent((float)t.x * sx, (float)t.y * sy);
        }
        bool now = (t.touched != 0);
        if (now != s_wasTouched) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, now);
            s_wasTouched = now;
        }
    }
}


// hotkey rebinding

void BeginHotkeyCapture()
{
    s_capturing    = true;
    s_captureArmed = false;   // wait for a clean release before listening
    s_captureMask  = 0;
}

bool IsCapturingHotkey()
{
    return s_capturing;
}

void HotkeyToString(uint32_t mask, char* out, int outSize)
{
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        { MB_L, "L" }, { MB_R, "R" }, { MB_ZL, "ZL" }, { MB_ZR, "ZR" },
        { MB_PLUS, "+" }, { MB_MINUS, "-" }, { MB_LSTICK, "L3" }, { MB_RSTICK, "R3" },
        { MB_A, "A" }, { MB_B, "B" }, { MB_X, "X" }, { MB_Y, "Y" },
        { MB_UP, "Up" }, { MB_DOWN, "Down" }, { MB_LEFT, "Left" }, { MB_RIGHT, "Right" },
    };
    if (outSize <= 0)
        return;
    out[0] = '\0';
    int len = 0;
    bool first = true;
    for (auto& e : kNames) {
        if (!(mask & e.bit))
            continue;
        const char* sep = first ? "" : "+";
        for (const char* p = sep; *p && len < outSize - 1; ++p) out[len++] = *p;
        for (const char* p = e.name; *p && len < outSize - 1; ++p) out[len++] = *p;
        out[len] = '\0';
        first = false;
    }
    if (first) {   // no bits set
        const char* none = "(none)";
        for (const char* p = none; *p && len < outSize - 1; ++p) out[len++] = *p;
        out[len] = '\0';
    }
}

} // namespace Input
