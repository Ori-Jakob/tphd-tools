// tools/link_position_editor.h -- edit Link's live position and facing.
#pragma once

namespace Tools {
namespace LinkPositionEditor {

// Checkbox shown in the Tools menu.
void DrawMenuItem();

bool IsEnabled();
void SetEnabled(bool enabled);

// Interactive editor window. It is only drawn while the menu is open.
void DrawWindow(bool menuActive);

} // namespace LinkPositionEditor
} // namespace Tools
