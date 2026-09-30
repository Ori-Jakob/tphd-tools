#include "ui/panels.h"

#include "core/config.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool drawHudToggle(const Control::Descriptor* d, Control::Surface)
{
    WindowState& win = d->token == 0 ? Hud::GameInfo::State() : Hud::InputViewer::State();
    const bool changed = ImGui::Checkbox(d->name, &win.enabled);
    if (changed)
        Config::MarkDirty();
    return changed;
}

static bool drawInputViewerOptions(const Control::Descriptor*, Control::Surface)
{
    if (!Hud::InputViewer::State().enabled)
        return false;
    ImGui::Indent();
    int percent = (int)(Hud::InputViewer::GetOpacity() * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(180.0f);
    bool changed = Field::SliderInt("Opacity##InputViewer", &percent, 0, 100, "%d%%");
    if (changed) {
        Hud::InputViewer::SetOpacity(percent / 100.0f);
        Config::MarkDirty();
    }
    bool ints = Hud::InputViewer::GetIntReadout();
    if (ImGui::Checkbox("Sticks as -127..127##InputViewer", &ints)) {
        Hud::InputViewer::SetIntReadout(ints);
        Config::MarkDirty();
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Instead of -1 to 1.");
    ImGui::Unindent();
    return changed;
}

void RegisterHudControls()
{
    static const Control::Descriptor kControls[] = {
        { "hud.game_info", "Game Info", "Camera & HUD / HUD",
          drawHudToggle, nullptr, nullptr, 0 },
        { "hud.input_viewer", "Input Viewer", "Camera & HUD / HUD",
          drawHudToggle, drawInputViewerOptions, nullptr, 1 },
    };
    for (const Control::Descriptor& d : kControls)
        Control::Register(d);
}

static void drawHudSection()
{
    ImGui::SeparatorText("HUD");
    Control::Draw("hud.game_info", Control::SURFACE_MENU);
    ImGui::SameLine();
    Hud::GameInfo::DrawSettingsButton();
    Control::Draw("hud.input_viewer", Control::SURFACE_MENU);
}

void DrawCameraHud()
{
    drawHudSection();
}
}
}
