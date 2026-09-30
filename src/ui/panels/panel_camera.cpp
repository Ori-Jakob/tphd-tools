#include "ui/panels.h"

#include "cheats/cheat_camera.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "tools/flycam.h"
#include "tools/modern_camera.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
enum ModernAxis {
    AXIS_X = 0,
    AXIS_Y,
};

static const float kSliderWidth = 200.0f;
static const char  kModernPath[] = "Camera & HUD / Modern Camera Settings";

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

static bool drawModernCamera(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Tools::ModernCamera::IsEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Tools::ModernCamera::SetEnabled(enabled);
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Right stick aims the camera.");
    return changed;
}

static bool drawSensitivity(const Control::Descriptor* d, Control::Surface surface)
{
    using namespace Tools::ModernCamera;
    const bool x = d->token == AXIS_X;
    float value = x ? GetSensitivityX() : GetSensitivityY();
    const char* label = d->name;
    if (surface == Control::SURFACE_MENU)
        label = x ? "Sensitivity X" : "Sensitivity Y";
    ImGui::SetNextItemWidth(kSliderWidth);
    if (!Field::SliderFloat(label, &value, 0.2f, 3.0f, "%.2f"))
        return false;
    if (x)
        SetSensitivityX(value);
    else
        SetSensitivityY(value);
    Config::MarkDirty();
    return true;
}

static bool drawFovScale(const Control::Descriptor* d, Control::Surface surface)
{
    float value = Tools::ModernCamera::GetFovScale();
    ImGui::SetNextItemWidth(kSliderWidth);
    if (!Field::SliderFloat(surface == Control::SURFACE_MENU ? "FOV scale" : d->name, &value,
                            0.60f, 1.50f, "x%.2f"))
        return false;
    Tools::ModernCamera::SetFovScale(value);
    Config::MarkDirty();
    return true;
}

static bool drawGlobalFov(const Control::Descriptor*, Control::Surface)
{
    bool global = Tools::ModernCamera::IsGlobalFovEnabled();
    const bool changed = ImGui::Checkbox("Global FOV", &global);
    if (changed) {
        Tools::ModernCamera::SetGlobalFovEnabled(global);
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also scales first person, lock-on, dialogue and cutscene cameras.");
    return changed;
}

static bool drawHorse(const Control::Descriptor* d, Control::Surface surface)
{
    bool enabled = Tools::ModernCamera::IsHorseEnabled();
    const bool changed = ImGui::Checkbox(
        surface == Control::SURFACE_MENU ? "While riding Epona" : d->name, &enabled);
    if (changed) {
        Tools::ModernCamera::SetHorseEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawInvert(const Control::Descriptor* d, Control::Surface surface)
{
    using namespace Tools::ModernCamera;
    const bool x = d->token == AXIS_X;
    bool enabled = x ? IsInvertXEnabled() : IsInvertYEnabled();
    const char* label = d->name;
    if (surface == Control::SURFACE_MENU)
        label = x ? "Invert X" : "Invert Y";
    const bool changed = ImGui::Checkbox(label, &enabled);
    if (changed) {
        if (x)
            SetInvertXEnabled(enabled);
        else
            SetInvertYEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawFreeFixedCamera(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Camera::FreeFixedCameraEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Uses the follow camera where the view is normally locked.");
    if (changed) {
        Cheats::Camera::SetFreeFixedCameraEnabled(enabled);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawFreeFixedScope(const Control::Descriptor*, Control::Surface)
{
    int scope = Cheats::Camera::GetScope();
    const int before = scope;
    static const char* const kScopes[] = { "Camera regions", "Regions and rooms", "Everywhere" };
    ImGui::BeginDisabled(!Cheats::Camera::FreeFixedCameraEnabled());
    ImGui::SetNextItemWidth(190.0f);
    ImGui::Combo("Applies to", &scope, kScopes, Cheats::Camera::SCOPE_COUNT);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cutscene cameras are never changed.");
    ImGui::EndDisabled();
    if (scope == before)
        return false;
    Cheats::Camera::SetScope(scope);
    Config::MarkDirty();
    return true;
}

void RegisterCameraControls()
{
    static const Control::Descriptor kControls[] = {
        { "camera.fly_cam", "Fly Cam", "Camera & HUD / Camera",
          drawFlyCam, nullptr, nullptr, 0 },
        { "camera.modern.enabled", "Modern Camera", "Camera & HUD / Camera",
          drawModernCamera, nullptr, nullptr, 0 },
        { "camera.modern.sensitivity_x", "Modern Camera Sensitivity X", kModernPath,
          drawSensitivity, nullptr, nullptr, AXIS_X },
        { "camera.modern.sensitivity_y", "Modern Camera Sensitivity Y", kModernPath,
          drawSensitivity, nullptr, nullptr, AXIS_Y },
        { "camera.modern.fov", "Modern Camera FOV", kModernPath,
          drawFovScale, drawGlobalFov, nullptr, 0 },
        { "camera.modern.horse", "Modern Camera on Epona", kModernPath,
          drawHorse, nullptr, nullptr, 0 },
        { "camera.modern.invert_x", "Modern Camera Invert X", kModernPath,
          drawInvert, nullptr, nullptr, AXIS_X },
        { "camera.modern.invert_y", "Modern Camera Invert Y", kModernPath,
          drawInvert, nullptr, nullptr, AXIS_Y },
    };
    for (const Control::Descriptor& d : kControls)
        Control::Register(d);

    Control::Descriptor freeFixed = { "camera.fixed.free", "Free Camera In Fixed-Camera Areas",
                                      "Camera & HUD / Camera Modifiers", drawFreeFixedCamera,
                                      drawFreeFixedScope, nullptr, 0 };
    Control::Register(freeFixed);
}

void DrawCameraMenuItems()
{
    Control::Draw("camera.fly_cam", Control::SURFACE_MENU);
    Control::Draw("camera.modern.enabled", Control::SURFACE_MENU);
    if (ImGui::BeginMenu("Modern Camera Settings")) {
        Control::Draw("camera.modern.sensitivity_x", Control::SURFACE_MENU);
        Control::Draw("camera.modern.sensitivity_y", Control::SURFACE_MENU);
        Control::Draw("camera.modern.fov", Control::SURFACE_MENU);
        Control::Draw("camera.modern.horse", Control::SURFACE_MENU);
        Control::Draw("camera.modern.invert_x", Control::SURFACE_MENU);
        ImGui::SameLine();
        Control::Draw("camera.modern.invert_y", Control::SURFACE_MENU);
        ImGui::EndMenu();
    }

    ImGui::SeparatorText("Camera Modifiers");
    Control::Draw("camera.fixed.free", Control::SURFACE_MENU);
}
}
}
