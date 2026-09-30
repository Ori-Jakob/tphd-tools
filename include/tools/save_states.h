#pragma once

#include "SSystem/SComponent/c_xyz.h"
#include "libtphd/types.h"

#include <stdint.h>

struct cJSON;

namespace Tools {
namespace SaveStates {
bool BeginGameSaveLoad(const char* sourceName, const void* image, uint32_t size);

void DrawMenuItem();
void DrawWindow();

bool WantsInputBlock(uint32_t held);

void Tick(bool acceptInput);
void OnApplicationStart();
void OnApplicationEnd();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
