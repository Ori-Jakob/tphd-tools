#include "cheats/cheat_difficulty.h"

#include "core/config.h"

#include "d/d_procname.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"

#include <coreinit/cache.h>
#include <stdint.h>

extern "C" {
volatile float g_tphdDamageReceivedMultiplier = 1.0f;
volatile uint32_t g_tphdDamageGivenQuarterScale = 4;
volatile uint8_t g_tphdRupeeMultiplier = 1;
volatile uint8_t g_tphdRupeeMode = 2;
volatile uint8_t g_tphdRupeesOnlyIncreaseEnabled = 0;
volatile uint8_t g_tphdInfiniteEnemyHealthEnabled = 0;
volatile uint8_t g_tphdNoFallDamageEnabled = 0;
volatile uint8_t g_tphdAlwaysFairyRevivalEnabled = 0;
}

namespace Cheats {
namespace Difficulty {
struct Entry {
    const char* name;
    const char* controlId;
    const char* configKey;
};

static const Entry kEntries[TOGGLE_COUNT] = {
    { "Rupees Only Increase",            "gameplay.difficulty.rupees_only_increase",  "diffRupeesOnlyIncrease" },
    { "Infinite Enemy Health",           "gameplay.difficulty.infinite_enemy_health", "diffInfiniteEnemyHealth" },
    { "No Fall Damage",                  "gameplay.difficulty.no_fall_damage",        "diffNoFallDamage" },
    { "Always Heal From Fairy On Death", "gameplay.difficulty.fairy_revival",          "diffFairyRevival" },
};

static const uint32_t kEnemyHealthOffset = 0x566u;
static const uint32_t kS1ActionOffset = 0x69Au;
static const int16_t  kS1MaxHealth = 50;
static const uint32_t kZsActionOffset = 0x664u;
static const uint32_t kZsModeOffset = 0x668u;
static const uint32_t kZsTargetOffset = 0x8E4u;
static const uint32_t kZsCollisionOffset = 0x8F8u;
static const int16_t  kZsMaxHealth = 20;

static bool  s_enabled[TOGGLE_COUNT];
static float s_damageReceived = 1.0f;
static float s_damageGiven = 1.0f;
static int   s_rupeeMultiplier = 1;
static int   s_rupeeMode = RUPEE_MODE_BOTH;

static bool valid(Toggle t) { return t >= 0 && t < TOGGLE_COUNT; }

bool IsEnabled(Toggle t)        { return valid(t) && s_enabled[t]; }
const char* Name(Toggle t)      { return valid(t) ? kEntries[t].name : "?"; }
const char* ControlId(Toggle t) { return valid(t) ? kEntries[t].controlId : ""; }

void SetEnabled(Toggle t, bool enabled)
{
    if (valid(t))
        s_enabled[t] = enabled;
}

static float clampQuarter(float value, float lo, float hi)
{
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return (float)(int)(value * 4.0f + 0.5f) * 0.25f;
}

float GetDamageReceived()          { return s_damageReceived; }
void  SetDamageReceived(float v)   { s_damageReceived = clampQuarter(v, 0.25f, 80.0f); }
float GetDamageGiven()             { return s_damageGiven; }
void  SetDamageGiven(float v)      { s_damageGiven = clampQuarter(v, 0.25f, 10.0f); }
int   GetRupeeMultiplier()         { return s_rupeeMultiplier; }
int   GetRupeeMode()               { return s_rupeeMode; }

void SetRupeeMultiplier(int value)
{
    s_rupeeMultiplier = value < 1 ? 1 : value > 10 ? 10 : value;
}

void SetRupeeMode(int mode)
{
    if (mode >= 0 && mode < RUPEE_MODE_COUNT)
        s_rupeeMode = mode;
}

template <typename T>
static void publish(volatile T* target, T value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(T));
}

template <typename T>
static volatile T& field(fopAc_ac_c* actor, uint32_t offset)
{
    return *(volatile T*)((uint8_t*)actor + offset);
}

static fopAc_ac_c* keepSpecialEnemiesAlive(fopAc_ac_c* actor, void*)
{
    if (!actor)
        return nullptr;
    switch (fopAcM_GetName(actor)) {
    case fpcNm_d_a_e_s1: {
        field<int16_t>(actor, kEnemyHealthOffset) = kS1MaxHealth;
        volatile int16_t& action = field<int16_t>(actor, kS1ActionOffset);
        if (action == 5 || action == 9 || action == 10)
            action = 0;
        break;
    }
    case fpcNm_d_a_e_zs: {
        volatile int32_t& action = field<int32_t>(actor, kZsActionOffset);
        if (action == 2) {
            action = 1;
            field<int32_t>(actor, kZsModeOffset) = 0;
            field<uint32_t>(actor, kZsTargetOffset) |= 1u;
            field<uint32_t>(actor, kZsCollisionOffset) |= 1u;
            field<int16_t>(actor, kEnemyHealthOffset) = kZsMaxHealth;
        }
        break;
    }
    default:
        break;
    }
    return nullptr;
}

void Tick()
{
    publish<float>(&g_tphdDamageReceivedMultiplier, s_damageReceived);
    publish<uint32_t>(&g_tphdDamageGivenQuarterScale, (uint32_t)(s_damageGiven * 4.0f + 0.5f));
    publish<uint8_t>(&g_tphdRupeeMultiplier, (uint8_t)s_rupeeMultiplier);
    publish<uint8_t>(&g_tphdRupeeMode, (uint8_t)s_rupeeMode);
    publish<uint8_t>(&g_tphdRupeesOnlyIncreaseEnabled, s_enabled[RUPEES_ONLY_INCREASE] ? 1 : 0);
    publish<uint8_t>(&g_tphdInfiniteEnemyHealthEnabled, s_enabled[INFINITE_ENEMY_HEALTH] ? 1 : 0);
    publish<uint8_t>(&g_tphdNoFallDamageEnabled, s_enabled[NO_FALL_DAMAGE] ? 1 : 0);
    publish<uint8_t>(&g_tphdAlwaysFairyRevivalEnabled, s_enabled[ALWAYS_FAIRY_REVIVAL] ? 1 : 0);
    if (s_enabled[INFINITE_ENEMY_HEALTH])
        fopAcIt_Judge(keepSpecialEnemiesAlive, nullptr);
}

void ResetToDefaults()
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        s_enabled[i] = false;
    s_damageReceived = 1.0f;
    s_damageGiven = 1.0f;
    s_rupeeMultiplier = 1;
    s_rupeeMode = RUPEE_MODE_BOTH;
}

void LoadConfig(const cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::ReadBool(root, kEntries[i].configKey, &s_enabled[i]);
    float f = s_damageReceived;
    if (Config::ReadFloat(root, "diffDamageReceived", &f, 0.25f, 80.0f))
        SetDamageReceived(f);
    f = s_damageGiven;
    if (Config::ReadFloat(root, "diffDamageGiven", &f, 0.25f, 10.0f))
        SetDamageGiven(f);
    Config::ReadInt(root, "diffRupeeMultiplier", &s_rupeeMultiplier, 1, 10);
    Config::ReadInt(root, "diffRupeeMode", &s_rupeeMode, 0, RUPEE_MODE_COUNT - 1);
}

void SaveConfig(cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::AddBool(root, kEntries[i].configKey, s_enabled[i]);
    Config::AddNumber(root, "diffDamageReceived", s_damageReceived);
    Config::AddNumber(root, "diffDamageGiven", s_damageGiven);
    Config::AddNumber(root, "diffRupeeMultiplier", s_rupeeMultiplier);
    Config::AddNumber(root, "diffRupeeMode", s_rupeeMode);
}
}
}
