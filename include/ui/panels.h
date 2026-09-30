#pragma once

namespace Ui {
namespace Panels {
void RegisterControls();
void RegisterGameplayControls();
void RegisterHudControls();
void RegisterCameraControls();

void DrawPractice();
void DrawGameplay();
void DrawCameraHud();
void DrawCameraMenuItems();
void DrawSettings();
void DrawResetConfirm();

void DrawHotkeysButton();
void DrawHotkeysWindow();

void DrawControlsButton();
void DrawControlsWindow();

#ifdef TPHD_TOOLS_DEBUG
void DrawDiagnostics();
#endif
}
}
