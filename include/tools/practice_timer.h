#pragma once

#include <stdint.h>

struct cJSON;

namespace Tools {
namespace PracticeTimer {
void RegisterControls();
void DrawMenuItem();
void DrawWindow(bool menuActive);

void Tick(bool acceptInput);
bool WantsInputBlock(uint32_t held);

void OnApplicationStart();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
