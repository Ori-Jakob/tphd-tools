// world_editor.h -- Live story, scene, and warp-state editor window.
#pragma once

namespace Cheats {
namespace WorldEditor {

// Checkbox synchronized with the editor window's open state.
void DrawMenuItem();

// The editor window. Drawn while the overlay menu bar is up.
void DrawWindow(bool menuActive);

bool IsOpen();
void SetOpen(bool open);

} // namespace WorldEditor
} // namespace Cheats
