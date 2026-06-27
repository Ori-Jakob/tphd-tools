// link_position.h -- passive Game Info HUD.
#pragma once

namespace Debug {
namespace LinkPosition {

// Checkboxes shown in the Debug menu.
void DrawMenuItem();

// Persisted state (for the config file).
bool IsEnabled();
void SetEnabled(bool enabled);
bool IsRealDateTimeEnabled();
void SetRealDateTimeEnabled(bool enabled);

// Persisted on-screen window position (for the config file). GetWindowPos
// returns the latest position (updated as the user drags it); SetWindowPos
// requests it be applied on the next draw.
void GetWindowPos(float* x, float* y);
void SetWindowPos(float x, float y);
void GetWindowSize(float* w, float* h);
void SetWindowSize(float w, float h);

// The info window. Visible whenever the feature is enabled. When `menuActive`
// (the Tools menu bar is up) the window is draggable/resizable; otherwise it is
// locked in place and ignores input (a passive HUD over the game).
void DrawWindow(bool menuActive);

} // namespace LinkPosition
} // namespace Debug
