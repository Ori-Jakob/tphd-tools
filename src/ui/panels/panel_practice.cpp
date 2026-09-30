#include "ui/panels.h"

#include "tools/position_editor.h"
#include "tools/warp.h"
#include "ui/quick_access.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
void RegisterControls()
{
    RegisterGameplayControls();
}

void DrawPractice()
{
    QuickAccess::DrawMenuItem();

    ImGui::SeparatorText("Navigation");
    Tools::Warp::DrawMenuItem();
    Tools::PositionEditor::DrawMenuItem();
}
}
}
