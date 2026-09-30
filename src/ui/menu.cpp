#include "ui/menu.h"

#include "render/image.h"
#include "tools/warp.h"
#include "ui/menu_nav.h"
#include "ui/panels.h"
#include "ui/quick_access.h"

#include "imgui.h"

#include "icon_32_bin.h"

namespace Ui {
namespace Menu {
static const float kLogoX = 7.0f;

void OnApplicationStart()
{
    Panels::RegisterControls();
    QuickAccess::OnApplicationStart();
}

void OnOpened()
{
    Nav::RequestMenuBarFocus();
}

static void drawToolbar(ImGuiIO& io)
{
    if (ImGui::BeginMainMenuBar()) {
        if (const Image::Texture* icon = Image::Load(icon_32_bin, icon_32_bin_size)) {
            const float side = ImGui::GetFrameHeight();
            ImGui::SetCursorPosX(kLogoX);
            ImGui::Image(icon->id, ImVec2(side, side));
        }
        if (ImGui::BeginMenu("Practice"))    { Panels::DrawPractice();    ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Gameplay"))    { Panels::DrawGameplay();    ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Settings"))    { Panels::DrawSettings();    ImGui::EndMenu(); }
#ifdef TPHD_TOOLS_DEBUG
        if (ImGui::BeginMenu("Diagnostics")) { Panels::DrawDiagnostics(); ImGui::EndMenu(); }
#endif
        ImGui::EndMainMenuBar();
    }

    Nav::ApplyMenuBarFocus(io);
}

void Draw(ImGuiIO& io)
{
    drawToolbar(io);
    Panels::DrawHotkeysWindow();
    Panels::DrawControlsWindow();
    Tools::Warp::DrawWindow();
    QuickAccess::DrawFullWindow();
    Panels::DrawResetConfirm();
}
}
}
