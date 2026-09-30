#include "ui/panels.h"

#include "app/cemu.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/settings.h"

#include "libtphd/reloc.h"
#include "m_Do/m_Do_graphic.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
void DrawDiagnostics()
{
    const Input::Snapshot& in = Input::Current();
    ImGui::Text("Input: %s", in.valid ? "reading" : "no controller");
    ImGui::Text("held 0x%04X  pressed 0x%04X", (unsigned)in.held, (unsigned)in.pressed);
    ImGui::Text("L (%.2f, %.2f)   R (%.2f, %.2f)",
                (double)in.lx, (double)in.ly, (double)in.rx, (double)in.ry);

    ImGui::Separator();

    ImGui::Text("Config: %s", Config::IsLoaded() ? "loaded" : "pending");
    if (Config::LastError()[0])
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", Config::LastError());
    ImGui::Text("Menu combo: %s", Input::DescribeCombo(Hotkeys::Get(Hotkeys::HOTKEY_MENU)));

    ImGui::Separator();

    ImGui::Text("Host: %s", App::g_underCemu ? "Cemu" : "rpl-loader");
    if (App::g_underCemu)
        ImGui::Text("Pack block: v%u", (unsigned)App::PackVersion());
    ImGui::Text("Data delta: %08X", (unsigned)g_tphdGameDataDelta);
    ImGui::Text("GamePad only: %s", mDoGph_isDrcOnly() ? "yes" : "no");
}
}
}
