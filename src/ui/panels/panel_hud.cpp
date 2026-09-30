#include "ui/panels.h"

#include "core/config.h"
#include "hud/hud_game_info.h"
#include "ui/ui_control.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool drawHudToggle(const Control::Descriptor* d, Control::Surface)
{
    WindowState& win = Hud::GameInfo::State();
    const bool changed = ImGui::Checkbox(d->name, &win.enabled);
    if (changed)
        Config::MarkDirty();
    return changed;
}

void RegisterHudControls()
{
    static const Control::Descriptor kControls[] = {
        { "hud.game_info", "Game Info", "Camera & HUD / HUD",
          drawHudToggle, nullptr, nullptr, 0 },
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
}

void DrawCameraHud()
{
    drawHudSection();
}
}
}
