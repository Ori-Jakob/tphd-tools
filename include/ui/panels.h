#pragma once

namespace Ui {
namespace Panels {
void RegisterControls();
void RegisterGameplayControls();

void DrawPractice();
void DrawGameplay();
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
