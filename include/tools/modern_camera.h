#pragma once

struct cJSON;

namespace Tools {
namespace ModernCamera {
bool  IsEnabled();
void  SetEnabled(bool enabled);
float GetSensitivityX();
void  SetSensitivityX(float sensitivity);
float GetSensitivityY();
void  SetSensitivityY(float sensitivity);
float GetFovScale();
void  SetFovScale(float scale);
bool  IsGlobalFovEnabled();
void  SetGlobalFovEnabled(bool enabled);
bool  IsHorseEnabled();
void  SetHorseEnabled(bool enabled);
bool  IsInvertXEnabled();
void  SetInvertXEnabled(bool enabled);
bool  IsInvertYEnabled();
void  SetInvertYEnabled(bool enabled);

void Tick(bool acceptInput);
void ResetToDefaults();

void OnApplicationStart();
void OnApplicationEnd();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
