#pragma once

#include "ui/window_state.h"

struct cJSON;

namespace Hud {
namespace GameInfo {
void DrawSettingsButton();
void DrawSettingsWindow();
void DrawWindow(bool menuActive);

Ui::WindowState& State();

void ResetToDefaults();
void OnApplicationStart();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
