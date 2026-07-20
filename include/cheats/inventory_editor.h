// inventory_editor.h -- Link inventory / save editor window.
//
// Opened from a checkbox in Gameplay > Items & Equipment. Edits the live save block (items,
// amounts, status, got-item flags). Modeled on dusklight's ImGuiSaveEditor.
#pragma once

namespace Cheats {
namespace InventoryEditor {

// Checkbox synchronized with the editor window's open state.
void DrawMenuItem();

// The editor window. Drawn while the overlay menu bar is up.
void DrawWindow(bool menuActive);

bool IsOpen();
void SetOpen(bool open);

} // namespace InventoryEditor
} // namespace Cheats
