#pragma once

struct cJSON;

namespace Cheats {
namespace Equipment {
enum Mod {
    SUPER_CLAWSHOT = 0,
    INFINITE_SPINNER,
    REMOTE_BOMBS,
    NO_BOMB_LIMIT,
    FAST_IRON_BOOTS,
    GREAT_SPIN,
    GREAT_SPIN_NO_SKILL,
    UNRESTRICTED_ITEMS,
    MOD_COUNT
};

enum SpinnerSpeed {
    SPINNER_DEFAULT = 0,
    SPINNER_SUPER,
    SPINNER_ULTRA,
    SPINNER_COUNT
};

bool        IsEnabled(Mod mod);
void        SetEnabled(Mod mod, bool enabled);
const char* Name(Mod mod);
const char* ControlId(Mod mod);

int         GetSpinnerSpeed();
void        SetSpinnerSpeed(int speed);
const char* SpinnerSpeedName(int speed);

void Tick(bool acceptInput);
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
