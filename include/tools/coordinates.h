#pragma once

#include <stdint.h>

struct cJSON;

namespace Tools {
namespace Coordinates {
void DrawMenuItem();

bool WantsInputBlock(uint32_t held);

void Tick(bool acceptInput);
void OnApplicationStart();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
