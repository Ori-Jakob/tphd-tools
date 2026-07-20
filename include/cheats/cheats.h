// cheats/cheats.h -- the Gameplay menu + per-frame cheat application.

#pragma once

namespace Cheats {

// Draws the workflow-oriented Gameplay menu and its nested modifier groups.
void DrawGameplayMenu();

// Called once per presented frame: applies every enabled cheat.
void Tick();

// Aroma title-process lifecycle.  Enabled settings persist, while ownership of
// Zelda.rpx code patches must be reset/released for each application lifetime.
void OnApplicationStart();
void OnApplicationEnd();

// --- persistence: let the config save/restore which cheats are enabled ---
// Cheats are addressed by name (stable across registry reordering) for storage.
int         Count();
const char* Name(int i);
bool        IsEnabled(int i);
void        SetEnabled(int i, bool on);

} // namespace Cheats
