// Equipment-specific gameplay modifiers and their native TPHD patches.

#pragma once

namespace Cheats {
namespace EquipmentModifiers {

void DrawMenu();
void Tick();

// Aroma keeps plugin statics across title processes.  These reset patch
// ownership for a new Zelda.rpx lifetime and restore owned patches on exit.
void OnApplicationStart();
void OnApplicationEnd();

// Boolean, name-addressed settings exposed through Cheats' existing config
// registry.  Spinner speed uses two hidden bits to persist its three choices.
int         ConfigCount();
const char* ConfigName(int i);
bool        ConfigEnabled(int i);
void        SetConfigEnabled(int i, bool on);

} // namespace EquipmentModifiers
} // namespace Cheats
