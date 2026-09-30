#pragma once

struct cJSON;

namespace Tools {
namespace FlyCam {
bool IsEnabled();
void SetEnabled(bool enabled);

bool IsActive();
void Stop();
bool WantsInputBlock();

void Tick(bool acceptInput);
void ResetToDefaults();

void OnApplicationStart();
void OnApplicationEnd();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
