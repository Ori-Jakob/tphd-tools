#pragma once

struct cJSON;

namespace Cheats {
namespace Difficulty {
enum Toggle {
    RUPEES_ONLY_INCREASE = 0,
    INFINITE_ENEMY_HEALTH,
    NO_FALL_DAMAGE,
    ALWAYS_FAIRY_REVIVAL,
    TOGGLE_COUNT
};

enum RupeeMode {
    RUPEE_MODE_ACQUIRING = 0,
    RUPEE_MODE_SPENDING,
    RUPEE_MODE_BOTH,
    RUPEE_MODE_COUNT
};

bool        IsEnabled(Toggle toggle);
void        SetEnabled(Toggle toggle, bool enabled);
const char* Name(Toggle toggle);
const char* ControlId(Toggle toggle);

float GetDamageReceived();
void  SetDamageReceived(float value);
float GetDamageGiven();
void  SetDamageGiven(float value);
int   GetRupeeMultiplier();
void  SetRupeeMultiplier(int value);
int   GetRupeeMode();
void  SetRupeeMode(int mode);

void Tick();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
