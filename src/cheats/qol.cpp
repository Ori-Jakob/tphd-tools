// Central TPHD quality-of-life movement and day-clock hooks.
//
// Addresses are verified against the US v81 Zelda.rpx and matched to the
// corresponding dusklight player/environment routines. Redirects stay
// installed for the title lifetime so Cemu cannot retain stale translations;
// 1.0 multipliers execute the original calculations unchanged.

#include "cheats/qol.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "code_patch.h"
#include "imgui.h"

extern "C" {
volatile float g_tphdClimbingSpeedMultiplier = 1.0f;
volatile float g_tphdClimbHeightMultiplier = 1.0f;
volatile float g_tphdBlockPushSpeedMultiplier = 1.0f;
volatile float g_tphdCrawlSpeedMultiplier = 1.0f;
volatile float g_tphdRollSpeedMultiplier = 1.0f;
volatile float g_tphdTimeSpeedMultiplier = 1.0f;

void tphdClimbingSpeedReturnHook();
void tphdWolfClimbAnimationHook();
void tphdWolfClimbParamHook();
void tphdHumanLedgeClimbAnimationHook();
void tphdClimbProbeHeightHook();
void tphdClimbDeltaHeightHook();
void tphdBlockPushSpeedReturnHook();
void tphdCrawlSpeedReturnHook();
void tphdRollFrontAnimationHook();
void tphdRollFrontSpeedHook();
void tphdRollSideAnimationHook();
void tphdRollSideSpeedHook();
void tphdTimeRateHook();
void tphdAlternateTimeRateHook();
}

namespace Cheats {
namespace QoL {

namespace {

using PatchWord = CodePatch::Word;
using PatchOwner = CodePatch::Owner;

struct HookSite {
    uint32_t address;
    uint32_t expected;
    const void* hook;
    bool link;
};

static float s_climbingSpeedMultiplier = 1.0f;
static float s_climbHeightMultiplier = 1.0f;
static float s_blockPushSpeedMultiplier = 1.0f;
static float s_crawlSpeedMultiplier = 1.0f;
static float s_rollSpeedMultiplier = 1.0f;
static float s_timeSpeedMultiplier = 1.0f;

static PatchOwner s_climbingSpeedPatch = {};
static PatchOwner s_climbHeightPatch = {};
static PatchOwner s_blockPushSpeedPatch = {};
static PatchOwner s_crawlSpeedPatch = {};
static PatchOwner s_rollSpeedPatch = {};
static PatchOwner s_timeSpeedPatch = {};

static volatile float* const kSavedTime =
    (volatile float*)0x1014537Cu;

static const HookSite kClimbingSpeedSites[] = {
    // Human ladder animation rate.
    { 0x0203F5D8u, 0x4E800020u,
      (const void*)tphdClimbingSpeedReturnHook, false },
    // Human wall/vine vertical movement.
    { 0x020A8EE0u, 0x4E800020u,
      (const void*)tphdClimbingSpeedReturnHook, false },
    // Human wall/vine side movement and ledge shimmy.
    { 0x020A6658u, 0x4E800020u,
      (const void*)tphdClimbingSpeedReturnHook, false },
    // Wolf Link's separate ledge-ready animation setup.
    { 0x0203FF7Cu, 0xC02A7090u,
      (const void*)tphdWolfClimbAnimationHook, true },
    // Wolf Link's descriptor-driven ledge grab/climb/land animations. This
    // replaces a leaf helper instruction, so preserve its caller LR with `b`.
    { 0x0202D624u, 0xC0250004u,
      (const void*)tphdWolfClimbParamHook, false },
    // Human ledge-top climb animation rate.
    { 0x020A6348u, 0xC02B7014u,
      (const void*)tphdHumanLedgeClimbAnimationHook, true },
};

static const HookSite kClimbHeightSites[] = {
    // Four form/mode branches converge on the same ledge-probe construction.
    { 0x02033FDCu, 0xEFF7B02Au,
      (const void*)tphdClimbProbeHeightHook, true },
    { 0x02034064u, 0xEFF7B02Au,
      (const void*)tphdClimbProbeHeightHook, true },
    { 0x020340E8u, 0xEFF7B02Au,
      (const void*)tphdClimbProbeHeightHook, true },
    { 0x02034164u, 0xEFF7B02Au,
      (const void*)tphdClimbProbeHeightHook, true },
    // Shared human/Wolf edge-height classification.
    { 0x0203443Cu, 0xECCBB828u,
      (const void*)tphdClimbDeltaHeightHook, true },
};

static const HookSite kBlockPushSpeedSites[] = {
    // getPushPullAnimeSpeed, shared by human and Wolf Link push/pull actions.
    { 0x02095820u, 0x4E800020u,
      (const void*)tphdBlockPushSpeedReturnHook, false },
};

static const HookSite kCrawlSpeedSites[] = {
    // Crawl/lie rate helper used by both human and Wolf Link.
    { 0x02053684u, 0x4E800020u,
      (const void*)tphdCrawlSpeedReturnHook, false },
};

static const HookSite kRollSpeedSites[] = {
    // Front-roll animation setup has dive-jump and normal entry branches.
    { 0x0204D150u, 0xC02A73E0u,
      (const void*)tphdRollFrontAnimationHook, true },
    { 0x0204D19Cu, 0xC02A73E0u,
      (const void*)tphdRollFrontAnimationHook, true },
    // Final clamped front-roll movement speed.
    { 0x0204D1FCu, 0x801F0574u,
      (const void*)tphdRollFrontSpeedHook, true },
    // Left/right side-roll animation and movement branches.
    { 0x0204C72Cu, 0xC029728Cu,
      (const void*)tphdRollSideAnimationHook, true },
    { 0x0204C778u, 0xC029728Cu,
      (const void*)tphdRollSideAnimationHook, true },
    { 0x0204C748u, 0xC18C73ACu,
      (const void*)tphdRollSideSpeedHook, true },
    { 0x0204C794u, 0xC18C73ACu,
      (const void*)tphdRollSideSpeedHook, true },
};

static const HookSite kTimeSpeedSites[] = {
    // dScnKy_env_light_c::setDaytime normal and alternate clock paths.
    { 0x02974A60u, 0xC11E1248u, (const void*)tphdTimeRateHook, true },
    { 0x02974CE0u, 0xC15E1248u,
      (const void*)tphdAlternateTimeRateHook, true },
};

template <unsigned Count>
static bool buildWords(const char* name, PatchOwner& owner,
                       const HookSite (&sites)[Count],
                       PatchWord (&words)[Count])
{
    for (unsigned i = 0; i < Count; ++i) {
        uint32_t branch = 0;
        const bool encoded = sites[i].link
            ? CodePatch::MakeHookBranch(name, owner, sites[i].address,
                                        sites[i].hook, &branch)
            : CodePatch::MakeHookJump(name, owner, sites[i].address,
                                      sites[i].hook, &branch);
        if (!encoded)
            return false;
        words[i] = { sites[i].address, sites[i].expected, branch };
    }
    return true;
}

template <unsigned Count>
static void applySites(const char* name, PatchOwner& owner,
                       const HookSite (&sites)[Count])
{
    PatchWord words[Count];
    if (buildWords(name, owner, sites, words))
        CodePatch::Apply(name, owner, words, (int)Count);
}

template <unsigned Count>
static void removeSites(const char* name, PatchOwner& owner,
                        const HookSite (&sites)[Count])
{
    PatchWord words[Count];
    if (buildWords(name, owner, sites, words))
        CodePatch::Remove(name, owner, words, (int)Count);
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

static void setFloat(volatile float* target, float value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
    OSMemoryBarrier();
}

static void installPermanentHooks()
{
    applySites("QoL climbing-speed hooks", s_climbingSpeedPatch,
               kClimbingSpeedSites);
    applySites("QoL climb-height hooks", s_climbHeightPatch,
               kClimbHeightSites);
    applySites("QoL block push/pull hook", s_blockPushSpeedPatch,
               kBlockPushSpeedSites);
    applySites("QoL crawl hook", s_crawlSpeedPatch, kCrawlSpeedSites);
    applySites("QoL roll hooks", s_rollSpeedPatch, kRollSpeedSites);
    applySites("QoL time-rate hooks", s_timeSpeedPatch, kTimeSpeedSites);
}

static void removePermanentHooks()
{
    removeSites("QoL time-rate hooks", s_timeSpeedPatch, kTimeSpeedSites);
    removeSites("QoL roll hooks", s_rollSpeedPatch, kRollSpeedSites);
    removeSites("QoL crawl hook", s_crawlSpeedPatch, kCrawlSpeedSites);
    removeSites("QoL block push/pull hook", s_blockPushSpeedPatch,
                kBlockPushSpeedSites);
    removeSites("QoL climb-height hooks", s_climbHeightPatch,
                kClimbHeightSites);
    removeSites("QoL climbing-speed hooks", s_climbingSpeedPatch,
                kClimbingSpeedSites);
}

static void publishSettings()
{
    setFloat(&g_tphdClimbingSpeedMultiplier, s_climbingSpeedMultiplier);
    setFloat(&g_tphdClimbHeightMultiplier, s_climbHeightMultiplier);
    setFloat(&g_tphdBlockPushSpeedMultiplier, s_blockPushSpeedMultiplier);
    setFloat(&g_tphdCrawlSpeedMultiplier, s_crawlSpeedMultiplier);
    setFloat(&g_tphdRollSpeedMultiplier, s_rollSpeedMultiplier);
    setFloat(&g_tphdTimeSpeedMultiplier, s_timeSpeedMultiplier);
}

static bool modifierSlider(const char* label, float* value,
                           float minimum, float maximum)
{
    ImGui::SetNextItemWidth(210.0f);
    if (!ImGui::SliderFloat(label, value, minimum, maximum, "%.2fx"))
        return false;
    *value = clampQuarter(*value, minimum, maximum);
    return true;
}

static void setCurrentTime(float time)
{
    if (time < 0.0f)
        time = 0.0f;
    if (time >= 360.0f)
        time = 359.999f;
    setFloat(kSavedTime, time);
}

} // namespace

void DrawMenu()
{
    bool changed = false;

    changed |= modifierSlider("Climbing Speed", &s_climbingSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ladders, vines/walls, ledge movement, and Wolf Link ledge climbing");

    changed |= modifierSlider("Climb Height", &s_climbHeightMultiplier,
                              0.25f, 4.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scale native ledge probe and grab heights for human and Wolf Link");

    changed |= modifierSlider("Block Push/Pull Speed",
                              &s_blockPushSpeedMultiplier, 0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Applies to human and Wolf Link block movement");

    changed |= modifierSlider("Crawling Speed", &s_crawlSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Applies to human crawl and Wolf Link crawl/lie movement");

    changed |= modifierSlider("Roll Speed / Distance", &s_rollSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scale front/side roll animation rate and movement velocity");

    ImGui::Separator();

    float time = *kSavedTime;
    if (time < 0.0f || time >= 360.0f)
        time = 0.0f;
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::SliderFloat("Time of Day", &time, 0.0f, 359.75f,
                           "%.2f"))
        setCurrentTime(time);
    int totalMinutes = (int)(time * 4.0f + 0.5f);
    if (totalMinutes >= 1440)
        totalMinutes = 1439;
    ImGui::SameLine();
    ImGui::TextDisabled("%02d:%02d", totalMinutes / 60,
                        totalMinutes % 60);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The native clock uses 0-360 units per day");

    changed |= modifierSlider("Time Progression Speed", &s_timeSpeedMultiplier,
                              0.0f, 20.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0x freezes normal clock progression; events retain their native clock rules");

    ImGui::Separator();
    if (ImGui::Button("Restore QoL Defaults")) {
        s_climbingSpeedMultiplier = 1.0f;
        s_climbHeightMultiplier = 1.0f;
        s_blockPushSpeedMultiplier = 1.0f;
        s_crawlSpeedMultiplier = 1.0f;
        s_rollSpeedMultiplier = 1.0f;
        s_timeSpeedMultiplier = 1.0f;
        changed = true;
    }

    if (changed)
        Tick();
}

void Tick()
{
    installPermanentHooks();
    publishSettings();
}

void OnApplicationStart()
{
    s_climbingSpeedPatch = {};
    s_climbHeightPatch = {};
    s_blockPushSpeedPatch = {};
    s_crawlSpeedPatch = {};
    s_rollSpeedPatch = {};
    s_timeSpeedPatch = {};
    publishSettings();
    installPermanentHooks();
}

void OnApplicationEnd()
{
    setFloat(&g_tphdClimbingSpeedMultiplier, 1.0f);
    setFloat(&g_tphdClimbHeightMultiplier, 1.0f);
    setFloat(&g_tphdBlockPushSpeedMultiplier, 1.0f);
    setFloat(&g_tphdCrawlSpeedMultiplier, 1.0f);
    setFloat(&g_tphdRollSpeedMultiplier, 1.0f);
    setFloat(&g_tphdTimeSpeedMultiplier, 1.0f);
    removePermanentHooks();
}

float GetClimbingSpeedMultiplier()
{
    return s_climbingSpeedMultiplier;
}

void SetClimbingSpeedMultiplier(float value)
{
    s_climbingSpeedMultiplier = clampQuarter(value, 0.25f, 5.0f);
}

float GetClimbHeightMultiplier()
{
    return s_climbHeightMultiplier;
}

void SetClimbHeightMultiplier(float value)
{
    s_climbHeightMultiplier = clampQuarter(value, 0.25f, 4.0f);
}

float GetBlockPushSpeedMultiplier()
{
    return s_blockPushSpeedMultiplier;
}

void SetBlockPushSpeedMultiplier(float value)
{
    s_blockPushSpeedMultiplier = clampQuarter(value, 0.25f, 5.0f);
}

float GetCrawlSpeedMultiplier()
{
    return s_crawlSpeedMultiplier;
}

void SetCrawlSpeedMultiplier(float value)
{
    s_crawlSpeedMultiplier = clampQuarter(value, 0.25f, 5.0f);
}

float GetRollSpeedMultiplier()
{
    return s_rollSpeedMultiplier;
}

void SetRollSpeedMultiplier(float value)
{
    s_rollSpeedMultiplier = clampQuarter(value, 0.25f, 5.0f);
}

float GetTimeSpeedMultiplier()
{
    return s_timeSpeedMultiplier;
}

void SetTimeSpeedMultiplier(float value)
{
    s_timeSpeedMultiplier = clampQuarter(value, 0.0f, 20.0f);
}

} // namespace QoL
} // namespace Cheats
