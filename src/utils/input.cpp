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
static HotkeyId s_captureTarget = HOTKEY_MENU;

// Capture completed onto a mask already used by another slot -- wait for the UI
// to confirm (clear the other slot) or cancel (discard).
static bool     s_conflictPending = false;
static HotkeyId s_pendingId       = HOTKEY_MENU;
static uint32_t s_pendingMask     = 0;

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
static bool     s_coordinateHotkeysArmed = false;
static bool     s_coordinateSavePending = false;
static bool     s_coordinateSaveFired = false;
static bool     s_coordinateLoadPending = false;
static bool     s_coordinateLoadFired = false;

// After the hotkey fires (esp. on close), keep zeroing ALL game input until the
// controller is fully released -- otherwise the buttons still held when the menu
// closes leak straight into gameplay (e.g. the L+R you closed with becomes a
// game action). Cleared in BeginFrame once no buttons remain held.
static bool     s_drainHeld = false;

// Quick-transform takeover: strip the combo's buttons from the game while held
// so e.g. Y doesn't equip an item, and report the combo's rising edge to the cheat.
static bool     s_qtArmed = false;   // cheat enabled? (set by Cheats each frame)
static bool     s_qtHeld  = false;   // combo held this frame (set in the hooks)
static bool     s_qtPrev  = false;   // combo held last frame (for the edge)
static bool     s_qtFired = false;   // rising edge, consumed by the cheat

static uint32_t* hotkeyPtr(HotkeyId id)
{
    switch (id) {
    case HOTKEY_MENU:              return &g_settings.hotkey;
    case HOTKEY_GAME_RESET:        return &g_settings.gameResetCombo;
    case HOTKEY_SAVE_STATE_RELOAD: return &g_settings.saveStateReloadCombo;
    case HOTKEY_QUICK_TRANSFORM:   return &g_settings.quickTransformCombo;
    case HOTKEY_FLY_CAM:           return &g_settings.flyCamCombo;
    case HOTKEY_MOON_JUMP:         return &g_settings.moonJumpCombo;
    case HOTKEY_SAVE_COORDINATES:  return &g_settings.saveCoordinatesCombo;
    case HOTKEY_LOAD_COORDINATES:  return &g_settings.loadCoordinatesCombo;
    case HOTKEY_REMOTE_BOMBS:      return &g_settings.remoteBombsCombo;
    default:                       return nullptr;
    }
}

// Neutral MenuButton bits -> device-native bits (for stripping a combo from the
// game's samples while it is held for Quick Transform).
static uint32_t unmapVpad(uint32_t m)
{
    uint32_t h = 0;
    if (m & MB_UP)     h |= VPAD_BUTTON_UP;
    if (m & MB_DOWN)   h |= VPAD_BUTTON_DOWN;
    if (m & MB_LEFT)   h |= VPAD_BUTTON_LEFT;
    if (m & MB_RIGHT)  h |= VPAD_BUTTON_RIGHT;
    if (m & MB_A)      h |= VPAD_BUTTON_A;
    if (m & MB_B)      h |= VPAD_BUTTON_B;
    if (m & MB_X)      h |= VPAD_BUTTON_X;
    if (m & MB_Y)      h |= VPAD_BUTTON_Y;
    if (m & MB_L)      h |= VPAD_BUTTON_L;
    if (m & MB_R)      h |= VPAD_BUTTON_R;
    if (m & MB_ZL)     h |= VPAD_BUTTON_ZL;
    if (m & MB_ZR)     h |= VPAD_BUTTON_ZR;
    if (m & MB_PLUS)   h |= VPAD_BUTTON_PLUS;
    if (m & MB_MINUS)  h |= VPAD_BUTTON_MINUS;
    if (m & MB_LSTICK) h |= VPAD_BUTTON_STICK_L;
    if (m & MB_RSTICK) h |= VPAD_BUTTON_STICK_R;
    return h;
}

static uint32_t unmapPro(uint32_t m)
{
    uint32_t h = 0;
    if (m & MB_UP)     h |= WPAD_PRO_BUTTON_UP;
    if (m & MB_DOWN)   h |= WPAD_PRO_BUTTON_DOWN;
    if (m & MB_LEFT)   h |= WPAD_PRO_BUTTON_LEFT;
    if (m & MB_RIGHT)  h |= WPAD_PRO_BUTTON_RIGHT;
    if (m & MB_A)      h |= WPAD_PRO_BUTTON_A;
    if (m & MB_B)      h |= WPAD_PRO_BUTTON_B;
    if (m & MB_X)      h |= WPAD_PRO_BUTTON_X;
    if (m & MB_Y)      h |= WPAD_PRO_BUTTON_Y;
    if (m & MB_L)      h |= WPAD_PRO_TRIGGER_L;
    if (m & MB_R)      h |= WPAD_PRO_TRIGGER_R;
    if (m & MB_ZL)     h |= WPAD_PRO_TRIGGER_ZL;
    if (m & MB_ZR)     h |= WPAD_PRO_TRIGGER_ZR;
    if (m & MB_PLUS)   h |= WPAD_PRO_BUTTON_PLUS;
    if (m & MB_MINUS)  h |= WPAD_PRO_BUTTON_MINUS;
    if (m & MB_LSTICK) h |= WPAD_PRO_BUTTON_STICK_L;
    if (m & MB_RSTICK) h |= WPAD_PRO_BUTTON_STICK_R;
    return h;
}

static uint32_t unmapClassic(uint32_t m)
{
    uint32_t h = 0;
    if (m & MB_UP)    h |= WPAD_CLASSIC_BUTTON_UP;
    if (m & MB_DOWN)  h |= WPAD_CLASSIC_BUTTON_DOWN;
    if (m & MB_LEFT)  h |= WPAD_CLASSIC_BUTTON_LEFT;
    if (m & MB_RIGHT) h |= WPAD_CLASSIC_BUTTON_RIGHT;
    if (m & MB_A)     h |= WPAD_CLASSIC_BUTTON_A;
    if (m & MB_B)     h |= WPAD_CLASSIC_BUTTON_B;
    if (m & MB_L)     h |= WPAD_CLASSIC_BUTTON_L;
    if (m & MB_R)     h |= WPAD_CLASSIC_BUTTON_R;
    if (m & MB_ZL)    h |= WPAD_CLASSIC_BUTTON_ZL;
    if (m & MB_ZR)    h |= WPAD_CLASSIC_BUTTON_ZR;
    if (m & MB_PLUS)  h |= WPAD_CLASSIC_BUTTON_PLUS;
    if (m & MB_MINUS) h |= WPAD_CLASSIC_BUTTON_MINUS;
    // Classic has no dedicated X/Y/L3/R3 in the same layout; Classic X/Y map via
    // the classic extension if present -- TPHD Classic uses A/B primarily.
    return h;
}

// True if any other rebindable slot already has exactly this mask.
static bool maskConflicts(HotkeyId self, uint32_t mask)
{
    if (mask == 0)
        return false;
    for (int i = 0; i < HOTKEY_COUNT; ++i) {
        if (i == (int)self)
            continue;
        uint32_t* p = hotkeyPtr((HotkeyId)i);
        if (p && *p == mask)
            return true;
    }
    return false;
}

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
    if (s_capturing || s_conflictPending)
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
    if (s_capturing || s_conflictPending || !g_settings.gameResetHotkey)
        return;
    uint32_t hk = g_settings.gameResetCombo;
    if (hk != 0 && neutral == hk && s_prev != hk) {
        s_gameResetPending = true;
        s_consumeFrame     = true;
        s_drainHeld        = true;
    }
}

static void detectSaveStateReloadHotkey(uint32_t neutral)
{
    if (s_capturing || s_conflictPending || !s_stateReloadArmed)
        return;
    uint32_t hk = g_settings.saveStateReloadCombo;
    if (hk != 0 && neutral == hk && s_prev != hk) {
        s_stateReloadPending = true;
        s_consumeFrame       = true;
        s_drainHeld          = true;
    }
}

static void detectCoordinateHotkeys(uint32_t neutral)
{
    if (s_capturing || s_conflictPending || !s_coordinateHotkeysArmed)
        return;

    const uint32_t save = g_settings.saveCoordinatesCombo;
    const uint32_t load = g_settings.loadCoordinatesCombo;
    if (save != 0 && neutral == save && s_prev != save) {
        s_coordinateSavePending = true;
        s_consumeFrame = true;
        s_drainHeld = true;
    } else if (load != 0 && neutral == load && s_prev != load) {
        s_coordinateLoadPending = true;
        s_consumeFrame = true;
        s_drainHeld = true;
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
    detectCoordinateHotkeys(bits);
    SwKbd::SetVpad(&b[0]);   // feed the system keyboard (before we zero it)

    // Quick transform: while the combo is held (and not in the menu), strip those
    // buttons from the game so e.g. Y can't equip an item -- transform wins.
    {
        uint32_t qt = g_settings.quickTransformCombo;
        if (s_qtArmed && !g_menuVisible && qt != 0 && (bits & qt) == qt) {
            s_qtHeld = true;
            uint32_t strip = unmapVpad(qt);
            for (int i = 0; i < n; i++) {
                b[i].hold    &= ~strip;
                b[i].trigger &= ~strip;
                b[i].release &= ~strip;
            }
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
    detectCoordinateHotkeys(bits);
    SwKbd::SetKpad(b, count);   // feed the system keyboard (before we zero it)

    // Quick transform: strip the combo's buttons (Pro + Classic) while held.
    {
        uint32_t qt = g_settings.quickTransformCombo;
        if (s_qtArmed && !g_menuVisible && qt != 0 && (bits & qt) == qt) {
            s_qtHeld = true;
            uint32_t stripPro = unmapPro(qt);
            uint32_t stripCl  = unmapClassic(qt);
            for (int i = 0; i < count && i < 4; i++) {
                b[i].pro.hold        &= ~stripPro;
                b[i].pro.trigger     &= ~stripPro;
                b[i].pro.release     &= ~stripPro;
                b[i].classic.hold    &= ~stripCl;
                b[i].classic.trigger &= ~stripCl;
                b[i].classic.release &= ~stripCl;
            }
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
                if (maskConflicts(s_captureTarget, s_captureMask)) {
                    s_pendingId       = s_captureTarget;
                    s_pendingMask     = s_captureMask;
                    s_conflictPending = true;
                } else {
                    uint32_t* p = hotkeyPtr(s_captureTarget);
                    if (p)
                        *p = s_captureMask;
                }
                s_capturing = false;
            }
        }
    }

    // The hotkey edge was already detected in the input hooks this frame.
    s_hotkeyFired   = s_togglePending;
    s_gameResetFired = s_gameResetPending;
    s_stateReloadFired = s_stateReloadPending;
    s_coordinateSaveFired = s_coordinateSavePending;
    s_coordinateLoadFired = s_coordinateLoadPending;
    s_togglePending = false;
    s_gameResetPending = false;
    s_stateReloadPending = false;
    s_coordinateSavePending = false;
    s_coordinateLoadPending = false;
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

bool SaveCoordinatesHotkeyFired()
{
    return s_coordinateSaveFired;
}

bool LoadCoordinatesHotkeyFired()
{
    return s_coordinateLoadFired;
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

void SetCoordinateHotkeysArmed(bool armed)
{
    s_coordinateHotkeysArmed = armed;
    if (!armed) {
        s_coordinateSavePending = false;
        s_coordinateSaveFired = false;
        s_coordinateLoadPending = false;
        s_coordinateLoadFired = false;
    }
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

const char* HotkeyName(HotkeyId id)
{
    switch (id) {
    case HOTKEY_MENU:              return "Open/Close Menu";
    case HOTKEY_GAME_RESET:        return "Game Reset";
    case HOTKEY_SAVE_STATE_RELOAD: return "Reload Last State";
    case HOTKEY_QUICK_TRANSFORM:   return "Quick Transform";
    case HOTKEY_FLY_CAM:           return "Fly Cam";
    case HOTKEY_MOON_JUMP:         return "Moon Jump";
    case HOTKEY_SAVE_COORDINATES:  return "Save Coordinates";
    case HOTKEY_LOAD_COORDINATES:  return "Load Coordinates";
    case HOTKEY_REMOTE_BOMBS:      return "Remote Bombs";
    default:                       return "";
    }
}

uint32_t GetHotkey(HotkeyId id)
{
    uint32_t* p = hotkeyPtr(id);
    return p ? *p : 0;
}

void SetHotkey(HotkeyId id, uint32_t mask)
{
    uint32_t* p = hotkeyPtr(id);
    if (p)
        *p = mask;
}

void BeginHotkeyCapture(HotkeyId id)
{
    if (id < 0 || id >= HOTKEY_COUNT)
        return;
    s_conflictPending = false;
    s_capturing       = true;
    s_captureArmed    = false;   // wait for a clean release before listening
    s_captureMask     = 0;
    s_captureTarget   = id;
}

void CancelHotkeyCapture()
{
    s_capturing       = false;
    s_captureArmed    = false;
    s_captureMask     = 0;
    s_conflictPending = false;
}

bool IsCapturingHotkey()
{
    return s_capturing;
}

HotkeyId CapturingHotkeyId()
{
    return s_captureTarget;
}

bool IsHotkeyConflictPending()
{
    return s_conflictPending;
}

HotkeyId PendingHotkeyId()
{
    return s_pendingId;
}

uint32_t PendingHotkeyMask()
{
    return s_pendingMask;
}

void HotkeyConflictNames(char* out, int outSize)
{
    if (outSize <= 0)
        return;
    out[0] = '\0';
    int len = 0;
    bool first = true;
    for (int i = 0; i < HOTKEY_COUNT; ++i) {
        if (i == (int)s_pendingId)
            continue;
        uint32_t* p = hotkeyPtr((HotkeyId)i);
        if (!p || *p != s_pendingMask)
            continue;
        const char* name = HotkeyName((HotkeyId)i);
        const char* sep = first ? "" : ", ";
        for (const char* q = sep; *q && len < outSize - 1; ++q) out[len++] = *q;
        for (const char* q = name; *q && len < outSize - 1; ++q) out[len++] = *q;
        out[len] = '\0';
        first = false;
    }
    if (first) {
        const char* none = "(unknown)";
        for (const char* q = none; *q && len < outSize - 1; ++q) out[len++] = *q;
        out[len] = '\0';
    }
}

void ConfirmHotkeyConflict()
{
    if (!s_conflictPending)
        return;
    // Clear every other slot that currently owns this mask, then assign it.
    for (int i = 0; i < HOTKEY_COUNT; ++i) {
        if (i == (int)s_pendingId)
            continue;
        uint32_t* p = hotkeyPtr((HotkeyId)i);
        if (p && *p == s_pendingMask)
            *p = 0;
    }
    uint32_t* dest = hotkeyPtr(s_pendingId);
    if (dest)
        *dest = s_pendingMask;
    s_conflictPending = false;
    s_pendingMask     = 0;
}

void CancelHotkeyConflict()
{
    s_conflictPending = false;
    s_pendingMask     = 0;
}

const HotkeyButtonLabel* GetHotkeyButtonLabels(int* count)
{
    static const HotkeyButtonLabel kLabels[] = {
        { MB_L, "L" }, { MB_R, "R" }, { MB_ZL, "ZL" }, { MB_ZR, "ZR" },
        { MB_PLUS, "+" }, { MB_MINUS, "-" }, { MB_LSTICK, "L3" }, { MB_RSTICK, "R3" },
        { MB_A, "A" }, { MB_B, "B" }, { MB_X, "X" }, { MB_Y, "Y" },
        { MB_UP, "Up" }, { MB_DOWN, "Down" }, { MB_LEFT, "Left" }, { MB_RIGHT, "Right" },
    };
    if (count)
        *count = (int)(sizeof(kLabels) / sizeof(kLabels[0]));
    return kLabels;
}

void HotkeyToString(uint32_t mask, char* out, int outSize)
{
    if (outSize <= 0)
        return;
    out[0] = '\0';
    int len = 0;
    bool first = true;
    int labelCount = 0;
    const HotkeyButtonLabel* labels = GetHotkeyButtonLabels(&labelCount);
    for (int i = 0; i < labelCount; ++i) {
        const HotkeyButtonLabel& e = labels[i];
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
