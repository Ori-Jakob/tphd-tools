// tools/warp.h -- Warps tool window (set stage / room / layer / spawn + warp).
#pragma once

namespace Tools {
namespace Warp {

// Checkbox shown in Practice > Navigation (toggles the window).
void DrawMenuItem();

bool IsEnabled();
void SetEnabled(bool enabled);

// The Warps window. Editable while the overlay menu bar is up; locked otherwise.
void DrawWindow(bool menuActive);

} // namespace Warp
} // namespace Tools
