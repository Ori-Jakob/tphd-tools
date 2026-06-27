// inventory_editor.h -- Link inventory / save editor window.
//
// Opened from a button in the Cheats menu. Edits the live save block (items,
// amounts, status, got-item flags). Modeled on dusklight's ImGuiSaveEditor.
#pragma once

namespace Cheats {
namespace InventoryEditor {

// Button shown in the Cheats menu (opens the window).
void DrawMenuButton();

// The editor window. Drawn while the Tools menu bar is up.
void DrawWindow(bool menuActive);

bool IsOpen();
void SetOpen(bool open);

} // namespace InventoryEditor
} // namespace Cheats
