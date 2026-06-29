// tools/input_viewer.h -- passive controller HUD.
#pragma once

namespace Tools {
namespace InputViewer {

// Checkbox shown in the Tools menu.
void DrawMenuItem();

// Persisted state for config.
bool IsEnabled();
void SetEnabled(bool enabled);
float GetOpacity();
void SetOpacity(float opacity);
void GetWindowPos(float* x, float* y);
void SetWindowPos(float x, float y);
void GetWindowSize(float* w, float* h);
void SetWindowSize(float w, float h);

// Drawn every frame when enabled. Draggable only while the menu is active.
void DrawWindow(bool menuActive);

} // namespace InputViewer
} // namespace Tools
