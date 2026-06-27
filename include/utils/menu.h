// menu.h -- the ImGui UI: load toast, settings, and (future) cheats/tools.
#pragma once

struct ImGuiIO;

namespace Menu {

void OnLoaded();          // start the "tools loaded" toast
void Toggle();            // show/hide the menu (called on the hotkey)

// Per frame, after Renderer::NewFrame: draws the toast (always, while active)
// and the menu window (when visible).
void Draw(ImGuiIO& io);

} // namespace Menu
