// ui_hotkey.cpp -- see ui_hotkey.h.
#include "ui_hotkey.h"

#include "input.h"
#include "imgui.h"

namespace UiHotkey {

static const ImVec4 kButtonGreen(64.0f / 255.0f, 207.0f / 255.0f,
                                  142.0f / 255.0f, 1.0f);

static void drawFragment(const char* text, const ImVec4& color, bool& first)
{
    if (!text || !text[0])
        return;
    if (!first)
        ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    first = false;
}

void DrawText(uint32_t mask, const char* prefix, const char* suffix, bool disabled)
{
    const ImVec4 baseColor = ImGui::GetStyleColorVec4(
        disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    bool first = true;
    drawFragment(prefix, baseColor, first);

    int labelCount = 0;
    const Input::HotkeyButtonLabel* labels =
        Input::GetHotkeyButtonLabels(&labelCount);
    bool drewButton = false;
    for (int i = 0; i < labelCount; ++i) {
        if (!(mask & labels[i].bit))
            continue;
        if (drewButton)
            drawFragment("+", baseColor, first);
        drawFragment(labels[i].name, kButtonGreen, first);
        drewButton = true;
    }

    if (!drewButton)
        drawFragment("(none)", baseColor, first);
    drawFragment(suffix, baseColor, first);
}

void Draw(uint32_t mask, bool disabled)
{
    DrawText(mask, nullptr, nullptr, disabled);
}

} // namespace UiHotkey
