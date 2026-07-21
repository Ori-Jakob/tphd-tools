// Difficulty and economy modifiers backed by permanent TPHD call-site hooks.

#pragma once

namespace Cheats {
namespace Difficulty {

enum RupeeMode {
    RUPEE_MODE_ACQUIRING = 0,
    RUPEE_MODE_SPENDING,
    RUPEE_MODE_BOTH,
};

void DrawMenu();
void Tick();
void OnApplicationStart();
void OnApplicationEnd();

// Boolean, name-addressed settings exposed through Cheats' config registry.
int         ConfigCount();
const char* ConfigName(int i);
bool        ConfigEnabled(int i);
void        SetConfigEnabled(int i, bool on);

// Numeric settings are persisted separately because they are not checkboxes.
float GetDamageReceivedMultiplier();
void  SetDamageReceivedMultiplier(float value);
float GetDamageGivenMultiplier();
void  SetDamageGivenMultiplier(float value);
int   GetRupeeMultiplier();
void  SetRupeeMultiplier(int value);
int   GetRupeeMode();
void  SetRupeeMode(int mode);

} // namespace Difficulty
} // namespace Cheats
