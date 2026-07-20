// ui_hotkey.h -- consistently render controller hotkey combinations in ImGui.
#pragma once

#include <stdint.h>

namespace UiHotkey {

// Button labels are green. Combination separators and optional surrounding
// text use the normal (or disabled) text color. This also means a physical
// controller Plus button is green while a separator using the same glyph is not.
void Draw(uint32_t mask, bool disabled = false);
void DrawText(uint32_t mask, const char* prefix, const char* suffix = nullptr,
              bool disabled = false);

} // namespace UiHotkey
