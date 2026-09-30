#include "ui/panels.h"

#include "tools/position_editor.h"
#include "tools/practice_timer.h"
#include "tools/roll_checker.h"
#include "tools/warp.h"
#include "ui/quick_access.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
void RegisterControls()
{
    RegisterGameplayControls();
    RegisterHudControls();
    Tools::RollChecker::RegisterControls();
    Tools::PracticeTimer::RegisterControls();
}

void DrawPractice()
{
    QuickAccess::DrawMenuItem();

    ImGui::SeparatorText("Timing");
    Tools::RollChecker::DrawMenuItem();
    Tools::PracticeTimer::DrawMenuItem();

    ImGui::SeparatorText("Navigation");
    Tools::Warp::DrawMenuItem();
    Tools::PositionEditor::DrawMenuItem();
}
}
}
