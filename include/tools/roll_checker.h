#pragma once

struct cJSON;

namespace Tools {
namespace RollChecker {
void RegisterControls();
void DrawMenuItem();
void DrawWindow(bool menuActive);

void Tick(bool acceptInput);

void OnApplicationStart();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
