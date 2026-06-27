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
bool GameResetHotkeyFired();                        // did Start+X+B fire?
bool SaveStateReloadHotkeyFired();                  // did ZL+ZR+Start fire?
void GetSnapshot(Snapshot* out);                    // latest combined neutral input
void FeedMenu(ImGuiIO& io, float dispW, float dispH); // nav + touch -> ImGui

// --- hotkey rebinding (used by the menu settings UI) ---
void BeginHotkeyCapture();
bool IsCapturingHotkey();
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

} // namespace Input
