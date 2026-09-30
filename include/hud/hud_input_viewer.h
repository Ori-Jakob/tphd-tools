#pragma once

#include "ui/window_state.h"

struct cJSON;

namespace Hud {
namespace InputViewer {
void DrawWindow(bool menuActive);

Ui::WindowState& State();

float GetOpacity();
void SetOpacity(float opacity);

bool GetIntReadout();
void SetIntReadout(bool on);

void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
