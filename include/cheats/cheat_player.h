#pragma once

#include <stdint.h>

struct cJSON;

namespace Cheats {
namespace Player {
enum Toggle {
    MOON_JUMP = 0,
    QUICK_TRANSFORM,
    INVINCIBILITY,
    INFINITE_HEARTS,
    INFINITE_AIR,
    INFINITE_AMMO,
    INFINITE_RUPEES,
    EPONA_DASHES,
    EPONA_DASH_SPEED,
    TOGGLE_COUNT
};

bool        IsEnabled(Toggle toggle);
void        SetEnabled(Toggle toggle, bool enabled);
const char* Name(Toggle toggle);
const char* ControlId(Toggle toggle);

bool WantsInputBlock(uint32_t held);

void Tick(bool acceptInput);
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
