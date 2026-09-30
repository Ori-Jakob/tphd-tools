#include "ui/panels.h"

#include "tools/position_editor.h"
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
}

void DrawPractice()
{
    QuickAccess::DrawMenuItem();

    ImGui::SeparatorText("Timing");
    Tools::RollChecker::DrawMenuItem();

    ImGui::SeparatorText("Navigation");
    Tools::Warp::DrawMenuItem();
    Tools::PositionEditor::DrawMenuItem();
}
}
}
