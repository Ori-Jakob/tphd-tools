#include "ui/panels.h"

#include "cheats/cheat_equipment.h"
#include "cheats/cheat_player.h"
#include "cheats/cheats.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "ui/notifications.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
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

static bool drawEquipmentToggle(const Control::Descriptor* d, Control::Surface)
{
    const Cheats::Equipment::Mod m = (Cheats::Equipment::Mod)d->token;
    bool enabled = Cheats::Equipment::IsEnabled(m);
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Equipment::SetEnabled(m, enabled);
        Config::MarkDirty();
    }
    switch (m) {
    case Cheats::Equipment::REMOTE_BOMBS:
        drawHotkeyHint(Hotkeys::HOTKEY_DETONATE_BOMBS, " (detonate ");
        break;
    case Cheats::Equipment::SUPER_CLAWSHOT:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Longer, faster Clawshot that latches onto more surfaces.");
        break;
    case Cheats::Equipment::NO_BOMB_LIMIT:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Place more than three bombs at once.");
        break;
    case Cheats::Equipment::FAST_IRON_BOOTS:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Normal speed and animations while wearing the Iron Boots.");
        break;
    case Cheats::Equipment::GREAT_SPIN:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Great Spin without full health.");
        break;
    case Cheats::Equipment::UNRESTRICTED_ITEMS:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use your sword and items where the game blocks them.");
        break;
    default:
        break;
    }
    return changed;
}

static bool drawGreatSpinSkill(const Control::Descriptor*, Control::Surface)
{
    using namespace Cheats::Equipment;
    int value = IsEnabled(GREAT_SPIN_NO_SKILL) ? 1 : 0;
    const int before = value;
    static const char* const kNames[] = { "Skill required", "Skill not required" };
    ImGui::BeginDisabled(!IsEnabled(GREAT_SPIN));
    ImGui::SetNextItemWidth(170.0f);
    ImGui::Combo("Great Spin skill", &value, kNames, 2);
    ImGui::EndDisabled();
    if (value == before)
        return false;
    SetEnabled(GREAT_SPIN_NO_SKILL, value == 1);
    Config::MarkDirty();
    return true;
}

static bool drawSpinnerSpeed(const Control::Descriptor* d, Control::Surface)
{
    using namespace Cheats::Equipment;
    int value = GetSpinnerSpeed();
    const int before = value;
    static const char* kNames[SPINNER_COUNT];
    for (int i = 0; i < SPINNER_COUNT; ++i)
        kNames[i] = SpinnerSpeedName(i);
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo(d->name, &value, kNames, SPINNER_COUNT);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ride speed only. Jumping off is unchanged.");
    if (value == before)
        return false;
    SetSpinnerSpeed(value);
    Config::MarkDirty();
    return true;
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

    static const char* const kEquipPaths[Cheats::Equipment::MOD_COUNT] = {
        "Gameplay / Items & Equipment / Equipment Modifiers",
        "Gameplay / Items & Equipment / Equipment Modifiers",
        "Gameplay / Items & Equipment / Equipment Modifiers",
        "Gameplay / Items & Equipment / Equipment Modifiers",
        "Gameplay / Items & Equipment / Equipment Modifiers",
        "Gameplay / Items & Equipment / Equipment Modifiers",
        nullptr,
        "Gameplay / Items & Equipment / Item Rules",
    };
    for (int i = 0; i < Cheats::Equipment::MOD_COUNT; ++i) {
        const Cheats::Equipment::Mod m = (Cheats::Equipment::Mod)i;
        if (!kEquipPaths[i])
            continue;
        Control::Descriptor d = { Cheats::Equipment::ControlId(m), Cheats::Equipment::Name(m),
                                  kEquipPaths[i], drawEquipmentToggle,
                                  m == Cheats::Equipment::GREAT_SPIN ? drawGreatSpinSkill : nullptr,
                                  nullptr, i };
        Control::Register(d);
    }
    Control::Descriptor spinner = { "gameplay.equipment.spinner_speed", "Spinner Speed",
                                    "Gameplay / Items & Equipment / Equipment Modifiers",
                                    drawSpinnerSpeed, nullptr, nullptr, 0 };
    Control::Register(spinner);
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

static void drawEquip(Cheats::Equipment::Mod m)
{
    Control::Draw(Cheats::Equipment::ControlId(m), Control::SURFACE_MENU);
}

static void drawEquipmentMenu()
{
    using namespace Cheats::Equipment;
    ImGui::SeparatorText("Clawshot");
    drawEquip(SUPER_CLAWSHOT);
    ImGui::SeparatorText("Spinner");
    drawEquip(INFINITE_SPINNER);
    Control::Draw("gameplay.equipment.spinner_speed", Control::SURFACE_MENU);
    ImGui::SeparatorText("Bombs");
    drawEquip(REMOTE_BOMBS);
    drawEquip(NO_BOMB_LIMIT);
    ImGui::SeparatorText("Movement Gear");
    drawEquip(FAST_IRON_BOOTS);
    ImGui::SeparatorText("Sword");
    drawEquip(GREAT_SPIN);

    ImGui::Separator();
    if (ImGui::Button("Restore all defaults")) {
        for (int i = 0; i < MOD_COUNT; ++i)
            if (i != UNRESTRICTED_ITEMS)
                SetEnabled((Mod)i, false);
        SetSpinnerSpeed(SPINNER_DEFAULT);
        Config::MarkDirty();
    }
}

static void drawItemsMenu()
{
    ImGui::SeparatorText("Resources");
    drawPlayer(Cheats::Player::INFINITE_AMMO);
    drawPlayer(Cheats::Player::INFINITE_RUPEES);

    ImGui::SeparatorText("Item Rules");
    drawEquip(Cheats::Equipment::UNRESTRICTED_ITEMS);

    ImGui::SeparatorText("Modifiers");
    if (ImGui::BeginMenu("Equipment Modifiers")) {
        drawEquipmentMenu();
        ImGui::EndMenu();
    }
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
