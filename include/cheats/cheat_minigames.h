#pragma once

struct cJSON;

namespace Cheats {
namespace Minigames {
enum Toggle {
    STAR_INSTANT_WIN = 0,
    RAPIDS_NO_LOSS,
    GOAT_INSTANT_WIN,
    FRUIT_COMBO_KEEP,
    EASY_SUMO,
    FREEZE_TIMERS,
    TOGGLE_COUNT
};

bool        IsEnabled(Toggle toggle);
void        SetEnabled(Toggle toggle, bool enabled);
const char* Name(Toggle toggle);
const char* ControlId(Toggle toggle);

int  GetRapidsMultiplier();
void SetRapidsMultiplier(int value);

void Tick(bool overlayHoldingPlay);
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
