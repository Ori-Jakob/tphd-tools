// utils/swkbd.h -- native Wii U software keyboard (nn::swkbd) for ImGui text fields.
//
// Self-contained driver (we don't compile imgui_impl_wiiu.cpp): when an ImGui
// InputText is focused (io.WantTextInput), we pop the system keyboard, feed it
// the controller input our hooks already stash, push the typed result back into
// ImGui, and draw it over the TV at present time. Keyboard input is handled
// here; nav/touch stays in input.cpp (suppressed while the keyboard is up).
#pragma once

struct VPADStatus;
struct KPADStatus;
struct ImGuiIO;

namespace SwKbd {

// Lazily create the keyboard (needs GX2 + FS up). Safe to call every frame;
// returns true once ready. Retries until Create() succeeds.
bool Init();
bool IsReady();

// True while the input form is on screen (fading in/visible/fading out).
bool IsVisible();

// Stash the latest controller state (called from the VPAD/KPAD read hooks,
// before they zero the game's copy). The keyboard reads these in ProcessInput.
void SetVpad(const VPADStatus* v);
void SetKpad(const KPADStatus* k, int count);

// Per-present: open the keyboard if a text field wants input, run Calc, and on
// OK push the typed string into the focused ImGui InputText. Returns true if the
// keyboard is up (caller should suppress menu nav for this frame).
bool ProcessInput(ImGuiIO& io);

// Draw the keyboard over the TV. Call at present, after the ImGui render, while
// the TV GX2 render target is still bound.
void DrawTV();
void DrawGamePad();

} // namespace SwKbd
