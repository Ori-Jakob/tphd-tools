// cheats/cheats.h -- the Cheats menu + per-frame cheat application.

#pragma once

namespace Cheats {

// Draws the contents of the "Cheats" menu (a checkbox per cheat).
void DrawMenu();

// Called once per presented frame: applies every enabled cheat.
void Tick();

// --- persistence: let the config save/restore which cheats are enabled ---
// Cheats are addressed by name (stable across registry reordering) for storage.
int         Count();
const char* Name(int i);
bool        IsEnabled(int i);
void        SetEnabled(int i, bool on);

} // namespace Cheats
