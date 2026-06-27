// debug_save.h -- Debug Save Loader.
//
// Lists both the dev saves shipped under /vol/content/DebugSaves and personal
// game saves under tphd_tools/saves on the active storage backend. Shipped
// saves use the game's debug loader; personal saves use the normal save-image
// deserializer and return-place resume rules. All files are read-only here.
#pragma once

namespace Debug {
namespace DebugSave {

// Checkbox shown in the Debug menu (toggles the window).
void DrawMenuItem();

bool IsEnabled();
void SetEnabled(bool enabled);

// The loader window (only drawn while the menu bar is up).
void DrawWindow(bool menuActive);

// Called once per presented frame: adopts background reads and queues the
// appropriate game/debug-save transition outside the ImGui draw.
void Tick();

} // namespace DebugSave
} // namespace Debug
