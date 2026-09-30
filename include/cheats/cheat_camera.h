#pragma once

struct cJSON;

namespace Cheats {
namespace Camera {
enum Scope {
    SCOPE_REGIONS = 0,
    SCOPE_ROOMS,
    SCOPE_EVERYWHERE,
    SCOPE_COUNT
};

bool FreeFixedCameraEnabled();
void SetFreeFixedCameraEnabled(bool enabled);
int  GetScope();
void SetScope(int scope);

void Tick();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
