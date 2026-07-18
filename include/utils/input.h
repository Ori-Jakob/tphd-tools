// input.h -- controller input: filtering game input + driving the menu.
//
// We never read the pads from the menu side (that would steal samples from the
// game's queue). Instead the codecave hooks the game's own VPADRead/KPADReadEx,
// and FilterVpad/FilterKpad both (a) stash the latest buttons for the menu and
// (b) neutralize the samples when blocking. The menu then consumes the stash.
#pragma once

#include <stdint.h>

struct ImGuiIO;

namespace Input {

enum InputSource : uint32_t {
    SOURCE_GAMEPAD = 1u << 0,
    SOURCE_PRO     = 1u << 1,
    SOURCE_CLASSIC = 1u << 2,
};

struct Snapshot {
    uint32_t buttons;
    uint32_t pressed;
    uint32_t sourceMask;
    float leftX;
    float leftY;
    float rightX;
    float rightY;
    bool connected;
};

// --- called from the codecave hooks (via OverlayEntry reasons 1/2) ---
// `buffers` is VPADStatus[count] / KPADStatus[count]; void* to keep main lean.
void FilterVpad(void* buffers, int count);
void FilterKpad(void* buffers, int count);

// --- called once per present, in this order ---
void BeginFrame();                                  // consume stash; hotkey/capture edges
bool HotkeyToggled();                               // did the open/close combo fire?
bool GameResetHotkeyFired();                        // did the game-reset combo fire?
bool SaveStateReloadHotkeyFired();                  // did the reload-last-state combo fire?
bool SaveCoordinatesHotkeyFired();                  // did save-coordinates fire?
bool LoadCoordinatesHotkeyFired();                  // did load-coordinates fire?
void GetSnapshot(Snapshot* out);                    // latest combined neutral input
void FeedMenu(ImGuiIO& io, float dispW, float dispH); // nav + touch -> ImGui

// --- rebindable feature hotkeys ---
// Each feature has a stored MenuButton mask (see ov::Settings). The rebind UI
// lists them all; capture commits on button release (same as the original menu
// hotkey rebind). Exact collisions with another assigned hotkey defer the
// commit until ConfirmHotkeyConflict / CancelHotkeyConflict.
enum HotkeyId : int {
    HOTKEY_MENU = 0,
    HOTKEY_GAME_RESET,
    HOTKEY_SAVE_STATE_RELOAD,
    HOTKEY_QUICK_TRANSFORM,
    HOTKEY_FLY_CAM,
    HOTKEY_MOON_JUMP,
    HOTKEY_SAVE_COORDINATES,
    HOTKEY_LOAD_COORDINATES,
    HOTKEY_COUNT
};

const char* HotkeyName(HotkeyId id);                // display name for the rebind table
uint32_t    GetHotkey(HotkeyId id);
void        SetHotkey(HotkeyId id, uint32_t mask);

// --- hotkey rebinding (used by the menu settings UI) ---
void BeginHotkeyCapture(HotkeyId id);
void CancelHotkeyCapture();
bool IsCapturingHotkey();
HotkeyId CapturingHotkeyId();                       // valid while capturing
bool IsHotkeyConflictPending();
HotkeyId PendingHotkeyId();
uint32_t PendingHotkeyMask();
// Writes a comma-separated list of feature names that already use the pending mask.
void HotkeyConflictNames(char* out, int outSize);
void ConfirmHotkeyConflict();                       // assign pending; clear colliding slots
void CancelHotkeyConflict();
void HotkeyToString(uint32_t mask, char* out, int outSize);

// --- quick-transform input takeover (Cheats) ---
// When armed, the input hooks strip the Y button out of the game's input on any
// frame the ZR+Y combo is held -- so the game never equips the Y-item -- and flag
// the rising edge so the cheat can fire the transform with nothing competing.
void SetQuickTransformArmed(bool armed);
bool QuickTransformFired();

// --- save-state reload hotkey takeover (Save Loader) ---
// When armed, ZL+ZR+Start is consumed by the input hooks and reported once on
// its rising edge. The Save Loader decides whether a remembered state exists.
void SetSaveStateReloadHotkeyArmed(bool armed);

// --- coordinate save/load hotkey takeover ---
// When armed, consume the configured combos and report their rising edges.
void SetCoordinateHotkeysArmed(bool armed);

} // namespace Input
