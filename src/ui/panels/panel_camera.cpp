#include "ui/panels.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "tools/flycam.h"
#include "ui/ui_control.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static bool drawFlyCam(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Tools::FlyCam::IsEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Tools::FlyCam::SetEnabled(enabled);
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Left stick: move");
        ImGui::TextUnformatted("Right stick: look");
        ImGui::TextUnformatted("ZR/ZL: up/down");
        ImGui::TextUnformatted("R/L: faster/slower");
        ImGui::TextUnformatted("Hold A/B: max/min speed");
        ImGui::TextUnformatted("L3: back to Link");
        ImGui::TextUnformatted("R3: move Link to the camera");
        ImGui::EndTooltip();
    }
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_FLY_CAM), " (", ")", true);
    return changed;
}

void RegisterCameraControls()
{
    static const Control::Descriptor kControls[] = {
        { "camera.fly_cam", "Fly Cam", "Camera & HUD / Camera",
          drawFlyCam, nullptr, nullptr, 0 },
    };
    for (const Control::Descriptor& d : kControls)
        Control::Register(d);
}

void DrawCameraMenuItems()
{
    Control::Draw("camera.fly_cam", Control::SURFACE_MENU);
}
}
}
