#include "cheats/cheat_equipment.h"

#include "core/config.h"
#include "core/hotkeys.h"

#include "d/d_com_inf_game.h"
#include "d/d_procname.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"

#include <coreinit/cache.h>
#include <stdint.h>

extern "C" {
volatile uint8_t g_tphdSuperClawshotEnabled = 0;
volatile uint8_t g_tphdInfiniteSpinnerTimeEnabled = 0;
volatile uint8_t g_tphdRemoteBombsEnabled = 0;
volatile uint8_t g_tphdUnrestrictedBombsEnabled = 0;
volatile uint8_t g_tphdUnrestrictedItemsEnabled = 0;
volatile uint8_t g_tphdFastIronBootsEnabled = 0;
volatile uint8_t g_tphdAlwaysGreatSpinMode = 0;
float g_tphdSpinnerSpeed = 26.0f;
float g_tphdFastIronBootsMultiplier = 1.0f;
float g_tphdClawshotBossShootSpeed = 150.0f;
float g_tphdClawshotShootSpeed = 100.0f;
float g_tphdClawshotRange = 2000.0f;
float g_tphdClawshotPullSpeed = 60.0f;
float g_tphdClawshotReturnSpeed = 150.0f;
}

namespace Cheats {
namespace Equipment {
struct Entry {
    const char* name;
    const char* controlId;
    const char* configKey;
};

static const Entry kEntries[MOD_COUNT] = {
    { "Super Clawshot",           "gameplay.equipment.super_clawshot",   "equipSuperClawshot" },
    { "Infinite Spinner Time",    "gameplay.equipment.infinite_spinner", "equipInfiniteSpinner" },
    { "Remote Bombs",             "gameplay.equipment.remote_bombs",     "equipRemoteBombs" },
    { "No Bomb Limit",            "gameplay.equipment.no_bomb_limit",    "equipNoBombLimit" },
    { "Fast Iron Boots",          "gameplay.equipment.fast_iron_boots",  "equipFastIronBoots" },
    { "Always Great Spin Attack", "gameplay.equipment.great_spin",       "equipGreatSpin" },
    { "Great Spin Without Skill", "gameplay.equipment.great_spin_skill", "equipGreatSpinNoSkill" },
    { "Unrestricted Items",       "gameplay.items.unrestricted",         "equipUnrestrictedItems" },
};

static const char* const kSpinnerNames[SPINNER_COUNT] = {
    "Default (26)", "Super (50)", "Ultra (100)",
};
static const float kSpinnerSpeeds[SPINNER_COUNT] = { 26.0f, 50.0f, 100.0f };

static const uint32_t kBombStateOffset = 0xB50u;
static const uint32_t kBombPlayerMade = 0x1u;
static const uint32_t kBombHit = 0x4u;

static bool s_enabled[MOD_COUNT];
static int  s_spinnerSpeed = SPINNER_DEFAULT;

static bool valid(Mod m) { return m >= 0 && m < MOD_COUNT; }

bool IsEnabled(Mod m)          { return valid(m) && s_enabled[m]; }
const char* Name(Mod m)        { return valid(m) ? kEntries[m].name : "?"; }
const char* ControlId(Mod m)   { return valid(m) ? kEntries[m].controlId : ""; }

void SetEnabled(Mod m, bool enabled)
{
    if (valid(m))
        s_enabled[m] = enabled;
}

int GetSpinnerSpeed() { return s_spinnerSpeed; }

void SetSpinnerSpeed(int speed)
{
    if (speed >= 0 && speed < SPINNER_COUNT)
        s_spinnerSpeed = speed;
}

const char* SpinnerSpeedName(int speed)
{
    return speed >= 0 && speed < SPINNER_COUNT ? kSpinnerNames[speed] : "?";
}

template <typename T>
static void publish(volatile T* target, T value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(T));
}

static void publishClawshot(bool on)
{
    publish<float>(&g_tphdClawshotBossShootSpeed, on ? 500.0f : 150.0f);
    publish<float>(&g_tphdClawshotShootSpeed, on ? 500.0f : 100.0f);
    publish<float>(&g_tphdClawshotRange, on ? 32000.0f : 2000.0f);
    publish<float>(&g_tphdClawshotPullSpeed, on ? 300.0f : 60.0f);
    publish<float>(&g_tphdClawshotReturnSpeed, on ? 500.0f : 150.0f);
    OSMemoryBarrier();
    publish<uint8_t>(&g_tphdSuperClawshotEnabled, on ? 1 : 0);
}

static void publishFlags()
{
    publishClawshot(s_enabled[SUPER_CLAWSHOT]);
    publish<uint8_t>(&g_tphdInfiniteSpinnerTimeEnabled, s_enabled[INFINITE_SPINNER] ? 1 : 0);
    publish<float>(&g_tphdSpinnerSpeed, kSpinnerSpeeds[s_spinnerSpeed]);
    publish<uint8_t>(&g_tphdRemoteBombsEnabled, s_enabled[REMOTE_BOMBS] ? 1 : 0);
    publish<uint8_t>(&g_tphdUnrestrictedBombsEnabled, s_enabled[NO_BOMB_LIMIT] ? 1 : 0);
    publish<uint8_t>(&g_tphdFastIronBootsEnabled, s_enabled[FAST_IRON_BOOTS] ? 1 : 0);
    publish<uint8_t>(&g_tphdUnrestrictedItemsEnabled, s_enabled[UNRESTRICTED_ITEMS] ? 1 : 0);
    const uint8_t spinMode = !s_enabled[GREAT_SPIN] ? 0 : s_enabled[GREAT_SPIN_NO_SKILL] ? 2 : 1;
    publish<uint8_t>(&g_tphdAlwaysGreatSpinMode, spinMode);
}

static fopAc_ac_c* detonatePlayerBomb(fopAc_ac_c* actor, void*)
{
    if (!actor || fopAcM_GetName(actor) != fpcNm_d_a_nbomb)
        return nullptr;
    volatile uint32_t* state = (volatile uint32_t*)((uint8_t*)actor + kBombStateOffset);
    if (*state & kBombPlayerMade)
        *state |= kBombHit;
    return nullptr;
}

void Tick(bool acceptInput)
{
    publishFlags();
    if (acceptInput && s_enabled[REMOTE_BOMBS] && dComIfGp_getPlayer() &&
        Hotkeys::Pressed(Hotkeys::HOTKEY_DETONATE_BOMBS))
        fopAcIt_Judge(detonatePlayerBomb, nullptr);
}

void ResetToDefaults()
{
    for (int i = 0; i < MOD_COUNT; ++i)
        s_enabled[i] = false;
    s_spinnerSpeed = SPINNER_DEFAULT;
}

void LoadConfig(const cJSON* root)
{
    for (int i = 0; i < MOD_COUNT; ++i)
        Config::ReadBool(root, kEntries[i].configKey, &s_enabled[i]);
    int speed = s_spinnerSpeed;
    if (Config::ReadInt(root, "equipSpinnerSpeed", &speed, 0, SPINNER_COUNT - 1))
        s_spinnerSpeed = speed;
}

void SaveConfig(cJSON* root)
{
    for (int i = 0; i < MOD_COUNT; ++i)
        Config::AddBool(root, kEntries[i].configKey, s_enabled[i]);
    Config::AddNumber(root, "equipSpinnerSpeed", s_spinnerSpeed);
}
}
}
