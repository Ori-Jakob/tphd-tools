#include "ui/panels.h"

#include "cheats/cheat_difficulty.h"
#include "cheats/cheat_equipment.h"
#include "cheats/cheat_minigames.h"
#include "cheats/cheat_movement.h"
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

static bool drawDifficultyToggle(const Control::Descriptor* d, Control::Surface)
{
    const Cheats::Difficulty::Toggle t = (Cheats::Difficulty::Toggle)d->token;
    bool enabled = Cheats::Difficulty::IsEnabled(t);
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Difficulty::SetEnabled(t, enabled);
        Config::MarkDirty();
    }
    if (t == Cheats::Difficulty::RUPEES_ONLY_INCREASE && ImGui::IsItemHovered())
        ImGui::SetTooltip("Losing rupees gives you that many instead.");
    else if (t == Cheats::Difficulty::ALWAYS_FAIRY_REVIVAL && ImGui::IsItemHovered())
        ImGui::SetTooltip("Revives you even without a fairy.");
    return changed;
}

static bool drawQuarterSlider(const char* label, float value, float lo, float hi,
                              void (*set)(float), const char* tooltip)
{
    ImGui::SetNextItemWidth(150.0f);
    const bool changed = Field::SliderFloat(label, &value, lo, hi, "%.2fx");
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    if (changed) {
        set(value);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawDamageReceived(const Control::Descriptor* d, Control::Surface)
{
    return drawQuarterSlider(d->name, Cheats::Difficulty::GetDamageReceived(), 0.25f, 80.0f,
                             Cheats::Difficulty::SetDamageReceived, nullptr);
}

static bool drawDamageGiven(const Control::Descriptor* d, Control::Surface)
{
    return drawQuarterSlider(d->name, Cheats::Difficulty::GetDamageGiven(), 0.25f, 10.0f,
                             Cheats::Difficulty::SetDamageGiven,
                             "Even a low setting still deals some damage.");
}

static bool drawRupeeMultiplier(const Control::Descriptor* d, Control::Surface)
{
    int value = Cheats::Difficulty::GetRupeeMultiplier();
    ImGui::SetNextItemWidth(150.0f);
    if (!Field::SliderInt(d->name, &value, 1, 10, "%dx"))
        return false;
    Cheats::Difficulty::SetRupeeMultiplier(value);
    Config::MarkDirty();
    return true;
}

static bool drawRupeeMode(const Control::Descriptor*, Control::Surface)
{
    int mode = Cheats::Difficulty::GetRupeeMode();
    const int before = mode;
    static const char* const kModes[] = { "Acquiring", "Spending", "Both" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("Affects", &mode, kModes, 3);
    if (mode == before)
        return false;
    Cheats::Difficulty::SetRupeeMode(mode);
    Config::MarkDirty();
    return true;
}

static bool drawMinigameToggle(const Control::Descriptor* d, Control::Surface)
{
    const Cheats::Minigames::Toggle t = (Cheats::Minigames::Toggle)d->token;
    bool enabled = Cheats::Minigames::IsEnabled(t);
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (changed) {
        Cheats::Minigames::SetEnabled(t, enabled);
        Config::MarkDirty();
    }
    if (!ImGui::IsItemHovered())
        return changed;
    switch (t) {
    case Cheats::Minigames::RAPIDS_NO_LOSS:
        ImGui::SetTooltip("Hitting rocks and walls no longer costs a point.");
        break;
    case Cheats::Minigames::FRUIT_COMBO_KEEP:
        ImGui::SetTooltip("Every fruit doubles your combo, even after switching types.");
        break;
    case Cheats::Minigames::EASY_SUMO:
        ImGui::SetTooltip("Your opponent leaves themselves open.");
        break;
    case Cheats::Minigames::FREEZE_TIMERS:
        ImGui::SetTooltip("Countdown timers stop. The menu never uses up time.");
        break;
    default:
        break;
    }
    return changed;
}

static bool drawRapidsMultiplier(const Control::Descriptor* d, Control::Surface)
{
    int value = Cheats::Minigames::GetRapidsMultiplier();
    ImGui::SetNextItemWidth(150.0f);
    const bool changed = Field::SliderInt(d->name, &value, 1, 10, "%dx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Points from each buoy. The score still stops at 99.");
    if (changed) {
        Cheats::Minigames::SetRapidsMultiplier(value);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawRate(const Control::Descriptor* d, Control::Surface)
{
    using namespace Cheats::Movement;
    const Rate r = (Rate)d->token;
    float value = GetRate(r);
    ImGui::SetNextItemWidth(210.0f);
    const bool changed = Field::SliderFloat(d->name, &value, RateMin(r), RateMax(r), "%.2fx");
    if (ImGui::IsItemHovered()) {
        switch (r) {
        case CLIMB_SPEED:  ImGui::SetTooltip("Ladders, vines and walls."); break;
        case CLIMB_HEIGHT: ImGui::SetTooltip("How high Link can grab a ledge."); break;
        case CRAWL_SPEED:  ImGui::SetTooltip("Human and wolf crawling."); break;
        case TIME_SPEED:   ImGui::SetTooltip("0x stops the clock."); break;
        default: break;
        }
    }
    if (changed) {
        SetRate(r, value);
        Config::MarkDirty();
    }
    return changed;
}

static bool drawTimeOfDay(const Control::Descriptor* d, Control::Surface)
{
    static float s_edit = 0.0f;
    static bool  s_editing = false;
    if (!s_editing)
        s_edit = Cheats::Movement::GetTimeOfDay();
    ImGui::SetNextItemWidth(210.0f);
    const bool changed = Field::SliderFloat(d->name, &s_edit, 0.0f, 359.75f, "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
    s_editing = ImGui::IsItemActive() || Field::IsSteeringSlider();
    if (changed)
        Cheats::Movement::SetTimeOfDay(s_edit);
    int minutes = (int)(s_edit * 4.0f + 0.5f);
    if (minutes > 1439)
        minutes = 1439;
    ImGui::SameLine();
    ImGui::TextDisabled("%02d:%02d", minutes / 60, minutes % 60);
    return changed;
}

static bool drawInstantText(const Control::Descriptor* d, Control::Surface)
{
    bool enabled = Cheats::Movement::InstantTextEnabled();
    const bool changed = ImGui::Checkbox(d->name, &enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Text shows at once and boxes advance while you hold B, cutscenes included.");
    if (changed) {
        Cheats::Movement::SetInstantTextEnabled(enabled);
        Config::MarkDirty();
    }
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

    static const char* const kDifficultyPaths[Cheats::Difficulty::TOGGLE_COUNT] = {
        "Gameplay / Difficulty / Economy",
        "Gameplay / Difficulty / Combat",
        "Gameplay / Difficulty / Survival",
        "Gameplay / Difficulty / Survival",
    };
    for (int i = 0; i < Cheats::Difficulty::TOGGLE_COUNT; ++i) {
        const Cheats::Difficulty::Toggle t = (Cheats::Difficulty::Toggle)i;
        Control::Descriptor d = { Cheats::Difficulty::ControlId(t), Cheats::Difficulty::Name(t),
                                  kDifficultyPaths[i], drawDifficultyToggle, nullptr, nullptr, i };
        Control::Register(d);
    }
    Control::Descriptor received = { "gameplay.difficulty.damage_received", "Damage Received",
                                     "Gameplay / Difficulty / Combat", drawDamageReceived,
                                     nullptr, nullptr, 0 };
    Control::Register(received);
    Control::Descriptor given = { "gameplay.difficulty.damage_given", "Damage Given",
                                  "Gameplay / Difficulty / Combat", drawDamageGiven,
                                  nullptr, nullptr, 0 };
    Control::Register(given);
    Control::Descriptor rupees = { "gameplay.difficulty.rupee_multiplier", "Rupee Multiplier",
                                   "Gameplay / Difficulty / Economy", drawRupeeMultiplier,
                                   drawRupeeMode, nullptr, 0 };
    Control::Register(rupees);

    static const char* const kMinigamePaths[Cheats::Minigames::TOGGLE_COUNT] = {
        "Gameplay / Minigames / STAR Game",
        "Gameplay / Minigames / Rapids Ride",
        "Gameplay / Minigames / Goat Herding",
        "Gameplay / Minigames / Fruit Pop Flight",
        "Gameplay / Minigames / Sumo Wrestling",
        "Gameplay / Minigames / Timers",
    };
    for (int i = 0; i < Cheats::Minigames::TOGGLE_COUNT; ++i) {
        const Cheats::Minigames::Toggle t = (Cheats::Minigames::Toggle)i;
#ifndef TPHD_TOOLS_EXPERIMENTAL
        if (t == Cheats::Minigames::FREEZE_TIMERS)
            continue;
#endif
        Control::Descriptor d = { Cheats::Minigames::ControlId(t), Cheats::Minigames::Name(t),
                                  kMinigamePaths[i], drawMinigameToggle, nullptr, nullptr, i };
        Control::Register(d);
    }
    Control::Descriptor rapids = { "gameplay.minigames.rapids_points", "Rapids Point Multiplier",
                                   "Gameplay / Minigames / Rapids Ride", drawRapidsMultiplier,
                                   nullptr, nullptr, 0 };
    Control::Register(rapids);

    for (int i = 0; i < Cheats::Movement::RATE_COUNT; ++i) {
        const Cheats::Movement::Rate r = (Cheats::Movement::Rate)i;
#ifndef TPHD_TOOLS_EXPERIMENTAL
        if (r == Cheats::Movement::BLOCK_SPEED || r == Cheats::Movement::CHAIN_SPEED)
            continue;
#endif
        const char* path = r == Cheats::Movement::TIME_SPEED
                               ? "Gameplay / Movement & World / World & Time"
                               : "Gameplay / Movement & World / Movement";
        Control::Descriptor d = { Cheats::Movement::RateControlId(r), Cheats::Movement::RateName(r),
                                  path, drawRate, nullptr, nullptr, i };
        Control::Register(d);
    }
    Control::Descriptor timeOfDay = { "gameplay.world.time_of_day", "Time of Day",
                                      "Gameplay / Movement & World / World & Time",
                                      drawTimeOfDay, nullptr, nullptr, 0 };
    Control::Register(timeOfDay);
    Control::Descriptor instantText = { "gameplay.world.instant_text", "Hold B for Instant Text",
                                        "Gameplay / Movement & World / Dialogue",
                                        drawInstantText, nullptr, nullptr, 0 };
    Control::Register(instantText);
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

static void drawDifficultyMenu()
{
    using namespace Cheats::Difficulty;
    ImGui::SeparatorText("Combat");
    Control::Draw("gameplay.difficulty.damage_received", Control::SURFACE_MENU);
    Control::Draw("gameplay.difficulty.damage_given", Control::SURFACE_MENU);
    Control::Draw(ControlId(INFINITE_ENEMY_HEALTH), Control::SURFACE_MENU);
    ImGui::SeparatorText("Economy");
    Control::Draw("gameplay.difficulty.rupee_multiplier", Control::SURFACE_MENU);
    Control::Draw(ControlId(RUPEES_ONLY_INCREASE), Control::SURFACE_MENU);
    ImGui::SeparatorText("Survival");
    Control::Draw(ControlId(NO_FALL_DAMAGE), Control::SURFACE_MENU);
    Control::Draw(ControlId(ALWAYS_FAIRY_REVIVAL), Control::SURFACE_MENU);

    ImGui::Separator();
    if (ImGui::Button("Restore difficulty defaults")) {
        ResetToDefaults();
        Config::MarkDirty();
    }
}

static void drawMinigamesMenu()
{
    using namespace Cheats::Minigames;
    ImGui::SeparatorText("STAR Game");
    Control::Draw(ControlId(STAR_INSTANT_WIN), Control::SURFACE_MENU);
    ImGui::SeparatorText("Goat Herding");
    Control::Draw(ControlId(GOAT_INSTANT_WIN), Control::SURFACE_MENU);
    ImGui::SeparatorText("Rapids Ride");
    Control::Draw(ControlId(RAPIDS_NO_LOSS), Control::SURFACE_MENU);
    Control::Draw("gameplay.minigames.rapids_points", Control::SURFACE_MENU);
    ImGui::SeparatorText("Fruit Pop Flight");
    Control::Draw(ControlId(FRUIT_COMBO_KEEP), Control::SURFACE_MENU);
    ImGui::SeparatorText("Sumo Wrestling");
    Control::Draw(ControlId(EASY_SUMO), Control::SURFACE_MENU);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    ImGui::SeparatorText("Timers");
    Control::Draw(ControlId(FREEZE_TIMERS), Control::SURFACE_MENU);
#endif

    ImGui::Separator();
    if (ImGui::Button("Restore minigame defaults")) {
        ResetToDefaults();
        Config::MarkDirty();
    }
}

static void drawRateControl(Cheats::Movement::Rate r)
{
    Control::Draw(Cheats::Movement::RateControlId(r), Control::SURFACE_MENU);
}

static void drawMovementMenu()
{
    using namespace Cheats::Movement;
    ImGui::SeparatorText("Epona");
    drawPlayer(Cheats::Player::EPONA_DASHES);
    drawPlayer(Cheats::Player::EPONA_DASH_SPEED);

    ImGui::SeparatorText("Movement");
    drawRateControl(CLIMB_SPEED);
    drawRateControl(CLIMB_HEIGHT);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    drawRateControl(BLOCK_SPEED);
    drawRateControl(CHAIN_SPEED);
#endif
    drawRateControl(CRAWL_SPEED);
    drawRateControl(ROLL_SPEED);

    ImGui::SeparatorText("World & Time");
    Control::Draw("gameplay.world.time_of_day", Control::SURFACE_MENU);
    drawRateControl(TIME_SPEED);

    ImGui::SeparatorText("Dialogue");
    Control::Draw("gameplay.world.instant_text", Control::SURFACE_MENU);

    ImGui::Separator();
    if (ImGui::Button("Restore movement defaults")) {
        ResetToDefaults();
        Config::MarkDirty();
    }
}

void DrawGameplay()
{
    if (ImGui::BeginMenu("Player"))            { drawPlayerMenu();   ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Items & Equipment")) { drawItemsMenu();    ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Difficulty"))        { drawDifficultyMenu(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Minigames"))         { drawMinigamesMenu(); ImGui::EndMenu(); }
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
