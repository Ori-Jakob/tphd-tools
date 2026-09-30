#include "ui/panels.h"

#include "cheats/cheat_player.h"
#include "cheats/cheats.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "ui/notifications.h"
#include "ui/ui_control.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
static void drawHotkeyHint(Hotkeys::Id id, const char* prefix)
{
    ImGui::SameLine(0.0f, 0.0f);
    Hotkey::DrawText(Hotkeys::Get(id), prefix, ")", true);
}

static bool drawPlayerToggle(const Control::Descriptor* d, Control::Surface)
{
    const Cheats::Player::Toggle t = (Cheats::Player::Toggle)d->token;
    bool enabled = Cheats::Player::IsEnabled(t);
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Player::SetEnabled(t, enabled);
        Config::MarkDirty();
    }
    if (t == Cheats::Player::MOON_JUMP)
        drawHotkeyHint(Hotkeys::HOTKEY_MOON_JUMP, " (hold ");
    else if (t == Cheats::Player::QUICK_TRANSFORM)
        drawHotkeyHint(Hotkeys::HOTKEY_QUICK_TRANSFORM, " (");
    else if (t == Cheats::Player::INFINITE_AMMO && ImGui::IsItemHovered())
        ImGui::SetTooltip("Arrows, bombs, seeds and lantern oil stay full.");
    else if (t == Cheats::Player::EPONA_DASHES && ImGui::IsItemHovered())
        ImGui::SetTooltip("All six spurs stay available.");
    return changed;
}

void RegisterGameplayControls()
{
    static const char* const kPaths[Cheats::Player::TOGGLE_COUNT] = {
        "Gameplay / Player",
        "Gameplay / Player",
        "Gameplay / Player",
        "Gameplay / Player",
        "Gameplay / Player",
        "Gameplay / Items & Equipment / Resources",
        "Gameplay / Items & Equipment / Resources",
        "Gameplay / Movement & World / Epona",
        "Gameplay / Movement & World / Epona",
    };
    for (int i = 0; i < Cheats::Player::TOGGLE_COUNT; ++i) {
        const Cheats::Player::Toggle t = (Cheats::Player::Toggle)i;
        Control::Descriptor d = { Cheats::Player::ControlId(t), Cheats::Player::Name(t),
                                  kPaths[i], drawPlayerToggle, nullptr, nullptr, i };
        Control::Register(d);
    }
}

static void drawPlayer(Cheats::Player::Toggle t)
{
    Control::Draw(Cheats::Player::ControlId(t), Control::SURFACE_MENU);
}

static void drawPlayerMenu()
{
    drawPlayer(Cheats::Player::MOON_JUMP);
    drawPlayer(Cheats::Player::QUICK_TRANSFORM);
    drawPlayer(Cheats::Player::INVINCIBILITY);
    drawPlayer(Cheats::Player::INFINITE_HEARTS);
    drawPlayer(Cheats::Player::INFINITE_AIR);
}

static void drawItemsMenu()
{
    ImGui::SeparatorText("Resources");
    drawPlayer(Cheats::Player::INFINITE_AMMO);
    drawPlayer(Cheats::Player::INFINITE_RUPEES);
}

static void drawMovementMenu()
{
    ImGui::SeparatorText("Epona");
    drawPlayer(Cheats::Player::EPONA_DASHES);
    drawPlayer(Cheats::Player::EPONA_DASH_SPEED);
}

void DrawGameplay()
{
    if (ImGui::BeginMenu("Player"))            { drawPlayerMenu();   ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Items & Equipment")) { drawItemsMenu();    ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Movement & World"))  { drawMovementMenu(); ImGui::EndMenu(); }

    ImGui::Separator();
    if (ImGui::Button("Turn everything off")) {
        Cheats::ResetToDefaults();
        Config::MarkDirty();
        Notifications::Show(Notifications::Info, "Gameplay", "Every cheat is off");
    }
}
}
}
