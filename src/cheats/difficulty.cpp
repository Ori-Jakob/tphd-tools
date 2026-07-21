// Central TPHD difficulty/economy hooks.
//
// The native addresses were verified against the US Zelda.rpx in Ghidra and
// matched to the corresponding dusklight routines. As with the equipment
// modifiers, executable redirects stay installed for the title lifetime so
// Cemu cannot retain a stale translation when a checkbox changes.

#include "cheats/difficulty.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "code_patch.h"
#include "game/d_actor.h"
#include "imgui.h"
#include "logger.h"

extern "C" {
volatile float g_tphdDamageReceivedMultiplier = 1.0f;
volatile uint32_t g_tphdDamageGivenQuarterScale = 4;
volatile uint8_t g_tphdRupeeMultiplier = 1;
volatile uint8_t g_tphdRupeeMode = 2;
volatile uint8_t g_tphdRupeesOnlyIncreaseEnabled = 0;
volatile uint8_t g_tphdInfiniteEnemyHealthEnabled = 0;
volatile uint8_t g_tphdNoFallDamageEnabled = 0;
volatile uint8_t g_tphdAlwaysFairyRevivalEnabled = 0;
volatile uint8_t g_tphdEasySumoEnabled = 0;

void tphdDamageReceivedHook();
void tphdDamageGivenHook();
void tphdRupeeChangeHook();
void tphdNoFallDamageHook();
void tphdAlwaysFairyRevivalHook();
void tphdEasySumoHook();
}

namespace Cheats {
namespace Difficulty {

namespace {

using PatchWord = CodePatch::Word;
using PatchOwner = CodePatch::Owner;

enum ConfigItem {
    CONFIG_RUPEES_ONLY_INCREASE = 0,
    CONFIG_INFINITE_ENEMY_HEALTH,
    CONFIG_NO_FALL_DAMAGE,
    CONFIG_ALWAYS_FAIRY_REVIVAL,
    CONFIG_EASY_SUMO,
    CONFIG_COUNT,
};

static const char* const kConfigNames[CONFIG_COUNT] = {
    "Rupees Only Increase",
    "Infinite Enemy Health",
    "No Fall Damage",
    "Always Heal From Fairy On Death",
    "Easy Sumo Wrestling",
};

static bool s_config[CONFIG_COUNT] = {};
static float s_damageReceivedMultiplier = 1.0f;
static float s_damageGivenMultiplier = 1.0f;
static int s_rupeeMultiplier = 1;
static int s_rupeeMode = RUPEE_MODE_BOTH;

static PatchOwner s_damageReceivedPatch = {};
static PatchOwner s_damageGivenPatch = {};
static PatchOwner s_rupeePatch = {};
static PatchOwner s_noFallDamagePatch = {};
static PatchOwner s_alwaysFairyPatch = {};
static PatchOwner s_easySumoPatch = {};

// TPHD actor offsets verified against the corresponding actor routines in
// Zelda.rpx. These two enemies can enter damage/death actions without relying
// solely on cc_at_check's shared health subtraction.
constexpr uint32_t ENEMY_HEALTH_OFFSET = 0x566u;

constexpr uint32_t E_S1_ACTION_OFFSET = 0x69Au;
constexpr int16_t E_S1_MAX_HEALTH = 50;

constexpr uint32_t E_ZS_ACTION_OFFSET = 0x664u;
constexpr uint32_t E_ZS_MODE_OFFSET = 0x668u;
constexpr uint32_t E_ZS_TARGET_SPRM_OFFSET = 0x8E4u;
constexpr uint32_t E_ZS_COLLISION_SPRM_OFFSET = 0x8F8u;
constexpr int16_t E_ZS_MAX_HEALTH = 20;

template <typename T>
static volatile T& actorField(fopAc_ac_c* actor, uint32_t offset)
{
    return *reinterpret_cast<volatile T*>(
        reinterpret_cast<uint8_t*>(actor) + offset);
}

static fopAc_ac_c* preserveSpecialEnemyHealth(fopAc_ac_c* actor, void*)
{
    if (!actor)
        return nullptr;

    switch (fopAcM_GetName(actor)) {
    case FPCNM_E_S1: {
        // Shadow Beasts have group-finisher and fail states which can set
        // health/action directly after the shared damage routine returns.
        actorField<int16_t>(actor, ENEMY_HEALTH_OFFSET) = E_S1_MAX_HEALTH;
        volatile int16_t& action =
            actorField<int16_t>(actor, E_S1_ACTION_OFFSET);
        if (action == 5 || action == 9 || action == 10)
            action = 0;
        break;
    }
    case FPCNM_E_ZS: {
        // Staltroops disable target/push collision while in their custom
        // damage action, so returning to wait must restore both set bits.
        volatile int32_t& action =
            actorField<int32_t>(actor, E_ZS_ACTION_OFFSET);
        if (action == 2) {
            action = 1;
            actorField<int32_t>(actor, E_ZS_MODE_OFFSET) = 0;
            volatile uint32_t& targetSPrm =
                actorField<uint32_t>(actor, E_ZS_TARGET_SPRM_OFFSET);
            volatile uint32_t& collisionSPrm =
                actorField<uint32_t>(actor, E_ZS_COLLISION_SPRM_OFFSET);
            targetSPrm = targetSPrm | 1u;
            collisionSPrm = collisionSPrm | 1u;
            actorField<int16_t>(actor, ENEMY_HEALTH_OFFSET) =
                E_ZS_MAX_HEALTH;
        }
        break;
    }
    default:
        break;
    }

    return nullptr;
}

static float clampQuarter(float value, float minimum, float maximum)
{
    if (value < minimum)
        value = minimum;
    if (value > maximum)
        value = maximum;
    const int quarters = (int)(value * 4.0f + 0.5f);
    return (float)quarters * 0.25f;
}

static bool setByte(volatile uint8_t* target, uint8_t value)
{
    if (*target == value)
        return false;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
    OSMemoryBarrier();
    return true;
}

static bool setWord(volatile uint32_t* target, uint32_t value)
{
    if (*target == value)
        return false;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
    OSMemoryBarrier();
    return true;
}

static bool setFloat(volatile float* target, float value)
{
    if (*target == value)
        return false;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
    OSMemoryBarrier();
    return true;
}

static bool buildNoFallDamagePatch(PatchWord* words, int* count)
{
    const struct Site {
        uint32_t address;
        uint32_t expected;
    } sites[] = {
        // Human landing, scripted/cut landing, and wolf landing respectively.
        { 0x0208C398u, 0x4BFFFA7Du },
        { 0x020A1C34u, 0x4BFEA1E1u },
        { 0x020BF018u, 0x4BFCCDFDu },
    };

    *count = (int)(sizeof(sites) / sizeof(sites[0]));
    for (int i = 0; i < *count; ++i) {
        uint32_t branch = 0;
        if (!CodePatch::MakeHookBranch(
                "No Fall Damage", s_noFallDamagePatch, sites[i].address,
                (const void*)tphdNoFallDamageHook, &branch))
            return false;
        words[i] = { sites[i].address, sites[i].expected, branch };
    }
    return true;
}

static void installPermanentHooks()
{
    PatchWord word = {};
    if (CodePatch::BuildSingleHook(
            "Damage Received Modifier", s_damageReceivedPatch,
            0x02A1C664u, 0xED29402Au,
            (const void*)tphdDamageReceivedHook, &word))
        CodePatch::Apply("Damage Received Modifier hook",
                         s_damageReceivedPatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "Damage Given Modifier", s_damageGivenPatch,
            0x02904118u, 0x7D074050u,
            (const void*)tphdDamageGivenHook, &word))
        CodePatch::Apply("Damage Given Modifier hook",
                         s_damageGivenPatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "Rupee Modifier", s_rupeePatch,
            0x02A1DB74u, 0x819F6280u,
            (const void*)tphdRupeeChangeHook, &word))
        CodePatch::Apply("Rupee Modifier hook", s_rupeePatch, &word, 1);

    PatchWord noFallDamage[3];
    int noFallDamageCount = 0;
    if (buildNoFallDamagePatch(noFallDamage, &noFallDamageCount))
        CodePatch::Apply("No Fall Damage hook", s_noFallDamagePatch,
                         noFallDamage, noFallDamageCount);

    if (CodePatch::BuildSingleHook(
            "Always Fairy Revival", s_alwaysFairyPatch,
            0x0203152Cu, 0x48A74F35u,
            (const void*)tphdAlwaysFairyRevivalHook, &word))
        CodePatch::Apply("Always Fairy Revival hook",
                         s_alwaysFairyPatch, &word, 1);

    // daNpcWrestler_c::sumouAI's common epilogue. The hook can replace the
    // selected response while r31 is still the wrestler, then performs the
    // displaced native restore instruction.
    if (CodePatch::BuildSingleHook(
            "Easy Sumo Wrestling", s_easySumoPatch,
            0x02667158u, 0x83E1000Cu,
            (const void*)tphdEasySumoHook, &word))
        CodePatch::Apply("Easy Sumo Wrestling hook", s_easySumoPatch,
                         &word, 1);
}

static void removePermanentHooks()
{
    PatchWord word = {};
    if (CodePatch::BuildSingleHook(
            "Damage Received Modifier", s_damageReceivedPatch,
            0x02A1C664u, 0xED29402Au,
            (const void*)tphdDamageReceivedHook, &word))
        CodePatch::Remove("Damage Received Modifier hook",
                          s_damageReceivedPatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "Damage Given Modifier", s_damageGivenPatch,
            0x02904118u, 0x7D074050u,
            (const void*)tphdDamageGivenHook, &word))
        CodePatch::Remove("Damage Given Modifier hook",
                          s_damageGivenPatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "Rupee Modifier", s_rupeePatch,
            0x02A1DB74u, 0x819F6280u,
            (const void*)tphdRupeeChangeHook, &word))
        CodePatch::Remove("Rupee Modifier hook", s_rupeePatch, &word, 1);

    PatchWord noFallDamage[3];
    int noFallDamageCount = 0;
    if (buildNoFallDamagePatch(noFallDamage, &noFallDamageCount))
        CodePatch::Remove("No Fall Damage hook", s_noFallDamagePatch,
                          noFallDamage, noFallDamageCount);

    if (CodePatch::BuildSingleHook(
            "Always Fairy Revival", s_alwaysFairyPatch,
            0x0203152Cu, 0x48A74F35u,
            (const void*)tphdAlwaysFairyRevivalHook, &word))
        CodePatch::Remove("Always Fairy Revival hook",
                          s_alwaysFairyPatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "Easy Sumo Wrestling", s_easySumoPatch,
            0x02667158u, 0x83E1000Cu,
            (const void*)tphdEasySumoHook, &word))
        CodePatch::Remove("Easy Sumo Wrestling hook", s_easySumoPatch,
                          &word, 1);
}

static void publishSettings()
{
    setFloat(&g_tphdDamageReceivedMultiplier,
             s_damageReceivedMultiplier);
    setWord(&g_tphdDamageGivenQuarterScale,
            (uint32_t)(s_damageGivenMultiplier * 4.0f + 0.5f));
    setByte(&g_tphdRupeeMultiplier, (uint8_t)s_rupeeMultiplier);
    setByte(&g_tphdRupeeMode, (uint8_t)s_rupeeMode);
    setByte(&g_tphdRupeesOnlyIncreaseEnabled,
            s_config[CONFIG_RUPEES_ONLY_INCREASE] ? 1 : 0);
    setByte(&g_tphdInfiniteEnemyHealthEnabled,
            s_config[CONFIG_INFINITE_ENEMY_HEALTH] ? 1 : 0);
    setByte(&g_tphdNoFallDamageEnabled,
            s_config[CONFIG_NO_FALL_DAMAGE] ? 1 : 0);
    setByte(&g_tphdAlwaysFairyRevivalEnabled,
            s_config[CONFIG_ALWAYS_FAIRY_REVIVAL] ? 1 : 0);
    setByte(&g_tphdEasySumoEnabled,
            s_config[CONFIG_EASY_SUMO] ? 1 : 0);
}

} // namespace

void DrawMenu()
{
    bool changed = false;

    ImGui::SeparatorText("Combat");
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::SliderFloat("Damage Received Modifier",
                           &s_damageReceivedMultiplier,
                           0.25f, 80.0f, "%.2fx")) {
        s_damageReceivedMultiplier =
            clampQuarter(s_damageReceivedMultiplier, 0.25f, 80.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("80x turns the minimum quarter-heart hit into maximum-health damage");

    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::SliderFloat("Damage Given Modifier",
                           &s_damageGivenMultiplier,
                           0.25f, 10.0f, "%.2fx")) {
        s_damageGivenMultiplier =
            clampQuarter(s_damageGivenMultiplier, 0.25f, 10.0f);
        changed = true;
    }
    changed |= ImGui::Checkbox(kConfigNames[CONFIG_INFINITE_ENEMY_HEALTH],
                               &s_config[CONFIG_INFINITE_ENEMY_HEALTH]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Prevent shared collision damage and repair known actor-specific damage states");

    ImGui::SeparatorText("Minigames");
    changed |= ImGui::Checkbox(kConfigNames[CONFIG_EASY_SUMO],
                               &s_config[CONFIG_EASY_SUMO]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Make the wrestler choose a native open response while preserving normal match and ring-out events");

    ImGui::SeparatorText("Economy");
    ImGui::SetNextItemWidth(150.0f);
    changed |= ImGui::SliderInt("Rupee Multiplier", &s_rupeeMultiplier,
                                1, 10, "%dx");

    const char* rupeeModes[] = { "Acquiring", "Spending", "Both" };
    ImGui::SetNextItemWidth(150.0f);
    changed |= ImGui::Combo("Affects", &s_rupeeMode, rupeeModes,
                            (int)(sizeof(rupeeModes) /
                                  sizeof(rupeeModes[0])));

    changed |= ImGui::Checkbox(
        kConfigNames[CONFIG_RUPEES_ONLY_INCREASE],
        &s_config[CONFIG_RUPEES_ONLY_INCREASE]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Convert every rupee loss into an equal gain");

    ImGui::SeparatorText("Survival");
    changed |= ImGui::Checkbox(kConfigNames[CONFIG_NO_FALL_DAMAGE],
                               &s_config[CONFIG_NO_FALL_DAMAGE]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use the game's safe-landing path instead of entering a fall-damage animation");
    changed |= ImGui::Checkbox(kConfigNames[CONFIG_ALWAYS_FAIRY_REVIVAL],
                               &s_config[CONFIG_ALWAYS_FAIRY_REVIVAL]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Spawn the native death-revival fairy even without a fairy or bottle");

    ImGui::SeparatorText("Reset");
    if (ImGui::Button("Restore Difficulty Defaults")) {
        s_damageReceivedMultiplier = 1.0f;
        s_damageGivenMultiplier = 1.0f;
        s_rupeeMultiplier = 1;
        s_rupeeMode = RUPEE_MODE_BOTH;
        for (int i = 0; i < CONFIG_COUNT; ++i)
            s_config[i] = false;
        changed = true;
    }

    if (changed)
        Tick();
}

void Tick()
{
    installPermanentHooks();
    publishSettings();
    if (s_config[CONFIG_INFINITE_ENEMY_HEALTH])
        fopAcIt_Judge(preserveSpecialEnemyHealth, nullptr);
}

void OnApplicationStart()
{
    s_damageReceivedPatch = {};
    s_damageGivenPatch = {};
    s_rupeePatch = {};
    s_noFallDamagePatch = {};
    s_alwaysFairyPatch = {};
    s_easySumoPatch = {};
    publishSettings();
    installPermanentHooks();
}

void OnApplicationEnd()
{
    setFloat(&g_tphdDamageReceivedMultiplier, 1.0f);
    setWord(&g_tphdDamageGivenQuarterScale, 4);
    setByte(&g_tphdRupeeMultiplier, 1);
    setByte(&g_tphdRupeeMode, RUPEE_MODE_BOTH);
    setByte(&g_tphdRupeesOnlyIncreaseEnabled, 0);
    setByte(&g_tphdInfiniteEnemyHealthEnabled, 0);
    setByte(&g_tphdNoFallDamageEnabled, 0);
    setByte(&g_tphdAlwaysFairyRevivalEnabled, 0);
    setByte(&g_tphdEasySumoEnabled, 0);
    removePermanentHooks();
}

int ConfigCount()
{
    return CONFIG_COUNT;
}

const char* ConfigName(int i)
{
    return i >= 0 && i < CONFIG_COUNT ? kConfigNames[i] : "";
}

bool ConfigEnabled(int i)
{
    return i >= 0 && i < CONFIG_COUNT && s_config[i];
}

void SetConfigEnabled(int i, bool on)
{
    if (i >= 0 && i < CONFIG_COUNT)
        s_config[i] = on;
}

float GetDamageReceivedMultiplier()
{
    return s_damageReceivedMultiplier;
}

void SetDamageReceivedMultiplier(float value)
{
    s_damageReceivedMultiplier = clampQuarter(value, 0.25f, 80.0f);
}

float GetDamageGivenMultiplier()
{
    return s_damageGivenMultiplier;
}

void SetDamageGivenMultiplier(float value)
{
    s_damageGivenMultiplier = clampQuarter(value, 0.25f, 10.0f);
}

int GetRupeeMultiplier()
{
    return s_rupeeMultiplier;
}

void SetRupeeMultiplier(int value)
{
    if (value < 1)
        value = 1;
    if (value > 10)
        value = 10;
    s_rupeeMultiplier = value;
}

int GetRupeeMode()
{
    return s_rupeeMode;
}

void SetRupeeMode(int mode)
{
    if (mode < RUPEE_MODE_ACQUIRING)
        mode = RUPEE_MODE_ACQUIRING;
    if (mode > RUPEE_MODE_BOTH)
        mode = RUPEE_MODE_BOTH;
    s_rupeeMode = mode;
}

} // namespace Difficulty
} // namespace Cheats
