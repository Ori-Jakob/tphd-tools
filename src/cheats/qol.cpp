// Central TPHD quality-of-life movement and day-clock hooks.
//
// Addresses are verified against the US v81 Zelda.rpx and matched to the
// corresponding dusklight player/environment routines. Redirects stay
// installed for the title lifetime so Cemu cannot retain stale translations;
// 1.0 multipliers execute the original calculations unchanged.

#include "cheats/qol.h"
#include "cheats/world_editor.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "code_patch.h"
#include "imgui.h"

extern "C" {
volatile float g_tphdClimbingSpeedMultiplier = 1.0f;
volatile float g_tphdClimbHeightMultiplier = 1.0f;
volatile float g_tphdBlockPushSpeedMultiplier = 1.0f;
volatile uint32_t g_tphdBlockPushSpeedQuarters = 4u;
volatile float g_tphdBemosMovePhase = 32768.0f / 13.0f;
volatile float g_tphdChainPullSpeedMultiplier = 1.0f;
volatile uint32_t g_tphdChainPullSpeedQuarters = 4u;
volatile uint32_t g_tphdQoLFrameSerial = 0u;
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
void tphdBlockPullDurationHook();
void tphdBlockPushDurationHook();
void tphdBemosDurationHook();
void tphdBemosPhaseHook();
void tphdChainWallDurationHook();
void tphdChainWallPhaseHook();
void tphdChainWallElapsedHook();
void tphdHumanChainMaxSpeedBlendHook();
void tphdHumanChainMaxSpeedActionHook();
void tphdWolfChainMaxSpeedActionHook();
void tphdHumanChainAnimationHeavyHook();
void tphdHumanChainAnimationNormalHook();
void tphdWolfChainAnimationHeavyHook();
void tphdWolfChainAnimationNormalHook();
void tphdLv4ChandelierCurveHook();
void tphdCrawlSpeedReturnHook();
void tphdRollFrontAnimationHook();
void tphdRollFrontSpeedHook();
void tphdRollSideAnimationHook();
void tphdRollSideSpeedHook();
void tphdTimeRateHook();
void tphdAlternateTimeRateHook();

struct TphdChandelierCurveState {
    const void* actor;
    int lastFrame;
    uint32_t lastTick;
    uint32_t speedQuarters;
};

static TphdChandelierCurveState s_tphdChandelierCurveStates[8] = {};

int tphdLv4ChandelierCurveDelta(const void* actor, int frame)
{
    // Obj_Lv4Chan advances one link from this ten-frame curve. When Link's
    // animation is sped up the native code skips entries; when slowed down it
    // applies entries repeatedly. Walk every crossed entry exactly once so the
    // chandelier remains locked to the animation cycle. Preserve the original
    // per-frame lookup at 1x.
    static const int curve[10] = { 0, 0, 0, 0, 0, 1, 1, 2, 2, 4 };

    if (frame < 0)
        frame = 0;
    if (frame > 9)
        frame = 9;

    const uint32_t quarters = g_tphdChainPullSpeedQuarters;
    if (quarters == 4u)
        return curve[frame];

    const uint32_t now = g_tphdQoLFrameSerial;
    int slot = -1;
    int freeSlot = -1;
    int oldestSlot = 0;
    uint32_t oldestAge = 0u;
    for (int i = 0; i < 8; ++i) {
        if (s_tphdChandelierCurveStates[i].actor == actor) {
            slot = i;
            break;
        }
        if (!s_tphdChandelierCurveStates[i].actor && freeSlot < 0)
            freeSlot = i;
        const uint32_t age = now - s_tphdChandelierCurveStates[i].lastTick;
        if (age >= oldestAge) {
            oldestAge = age;
            oldestSlot = i;
        }
    }

    if (slot < 0)
        slot = freeSlot >= 0 ? freeSlot : oldestSlot;

    TphdChandelierCurveState& state = s_tphdChandelierCurveStates[slot];
    const uint32_t age = now - state.lastTick;
    int delta = curve[frame];
    if (state.actor == actor && state.speedQuarters == quarters && age <= 32u) {
        delta = 0;
        if (frame > state.lastFrame) {
            for (int i = state.lastFrame + 1; i <= frame; ++i)
                delta += curve[i];
        } else if (frame < state.lastFrame) {
            for (int i = state.lastFrame + 1; i < 10; ++i)
                delta += curve[i];
            for (int i = 0; i <= frame; ++i)
                delta += curve[i];
        }
    }

    state.actor = actor;
    state.lastFrame = frame;
    state.lastTick = now;
    state.speedQuarters = quarters;
    return delta;
}
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
static float s_chainPullSpeedMultiplier = 1.0f;
static float s_crawlSpeedMultiplier = 1.0f;
static float s_rollSpeedMultiplier = 1.0f;
static float s_timeSpeedMultiplier = 1.0f;

static PatchOwner s_climbingSpeedPatch = {};
static PatchOwner s_climbHeightPatch = {};
#ifdef TPHD_TOOLS_EXPERIMENTAL
static PatchOwner s_blockPushSpeedPatch = {};
static PatchOwner s_chainPullSpeedPatch = {};
#endif
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

#ifdef TPHD_TOOLS_EXPERIMENTAL
static const HookSite kBlockPushSpeedSites[] = {
    // getPushPullAnimeSpeed, shared by human and Wolf Link push/pull actions.
    { 0x02095820u, 0x4E800020u,
      (const void*)tphdBlockPushSpeedReturnHook, false },
    // daObjMovebox::Act_c::mode_wait starts the block's own eased movement
    // timer after Link's action has engaged. Leave the first/repeat engagement
    // thresholds native so releasing the stick cannot queue another move.
    { 0x027D11F4u, 0xA80C000Au,
      (const void*)tphdBlockPullDurationHook, true },
    { 0x027D1254u, 0xA8EC0004u,
      (const void*)tphdBlockPushDurationHook, true },
    // Obj_bm (the pushable Beamos/statue actor) uses a separate fixed
    // 13-frame eased move rather than daObjMovebox's attribute table.
    { 0x026C515Cu, 0x3800000Du,
      (const void*)tphdBemosDurationHook, true },
    { 0x026C5160u, 0xC1ACCF98u,
      (const void*)tphdBemosPhaseHook, true },
};

static const HookSite kChainPullSpeedSites[] = {
    // daObjCwall_c has an independent eased movement timer. Scale its duration,
    // phase step, and elapsed-frame calculation with the same chain setting.
    // initWalk is a leaf function, so its two redirects must preserve LR.
    { 0x026FD65Cu, 0x3960000Du,
      (const void*)tphdChainWallDurationHook, false },
    { 0x026FD670u, 0x398009D8u,
      (const void*)tphdChainWallPhaseHook, false },
    { 0x026FD8A0u, 0x210B000Du,
      (const void*)tphdChainWallElapsedHook, true },
    // Human Link's chain-specific maximum movement speed is selected in both
    // the animation blend and per-frame action paths.
    { 0x0202C30Cu, 0xC14C7474u,
      (const void*)tphdHumanChainMaxSpeedBlendHook, true },
    { 0x02041CDCu, 0xC00A7474u,
      (const void*)tphdHumanChainMaxSpeedActionHook, true },
    // Wolf Link's per-frame chain movement limit.
    { 0x020523DCu, 0xC00C7474u,
      (const void*)tphdWolfChainMaxSpeedActionHook, true },
    // Heavy chain actors use a native 0.7 animation rate; other chains use
    // 1.0. Scale both branches for human and Wolf Link.
    { 0x0202C458u, 0xC0297314u,
      (const void*)tphdHumanChainAnimationHeavyHook, true },
    { 0x0202C474u, 0xFC20E890u,
      (const void*)tphdHumanChainAnimationNormalHook, true },
    { 0x02051B08u, 0xC02A7314u,
      (const void*)tphdWolfChainAnimationHeavyHook, true },
    { 0x02051B4Cu, 0xFC20F890u,
      (const void*)tphdWolfChainAnimationNormalHook, true },
    // Arbiter's Grounds' Obj_Lv4Chan actor samples a ten-entry movement curve
    // from Link's animation frame. Integrate skipped/repeated curve entries at
    // both the negative-frame fallback and ordinary indexed lookup.
    { 0x02795234u, 0x800BCF8Cu,
      (const void*)tphdLv4ChandelierCurveHook, true },
    { 0x0279525Cu, 0x800BCF8Cu,
      (const void*)tphdLv4ChandelierCurveHook, true },
};
#endif

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

#ifdef TPHD_TOOLS_EXPERIMENTAL
static void setU32(volatile uint32_t* target, uint32_t value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
    OSMemoryBarrier();
}

static uint32_t scaledDuration(uint32_t nativeFrames, uint32_t speedQuarters)
{
    uint32_t frames = (nativeFrames * 4u + speedQuarters / 2u) /
                      speedQuarters;
    return frames > 0u ? frames : 1u;
}

static void resetChandelierCurveStates()
{
    for (int i = 0; i < 8; ++i)
        s_tphdChandelierCurveStates[i] = {};
}
#endif

static void installPermanentHooks()
{
    applySites("QoL climbing-speed hooks", s_climbingSpeedPatch,
               kClimbingSpeedSites);
    applySites("QoL climb-height hooks", s_climbHeightPatch,
               kClimbHeightSites);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    applySites("QoL block push/pull hook", s_blockPushSpeedPatch,
               kBlockPushSpeedSites);
    applySites("QoL chain pull hooks", s_chainPullSpeedPatch,
               kChainPullSpeedSites);
#endif
    applySites("QoL crawl hook", s_crawlSpeedPatch, kCrawlSpeedSites);
    applySites("QoL roll hooks", s_rollSpeedPatch, kRollSpeedSites);
    applySites("QoL time-rate hooks", s_timeSpeedPatch, kTimeSpeedSites);
}

static void removePermanentHooks()
{
    removeSites("QoL time-rate hooks", s_timeSpeedPatch, kTimeSpeedSites);
    removeSites("QoL roll hooks", s_rollSpeedPatch, kRollSpeedSites);
    removeSites("QoL crawl hook", s_crawlSpeedPatch, kCrawlSpeedSites);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    removeSites("QoL chain pull hooks", s_chainPullSpeedPatch,
                kChainPullSpeedSites);
    removeSites("QoL block push/pull hook", s_blockPushSpeedPatch,
                kBlockPushSpeedSites);
#endif
    removeSites("QoL climb-height hooks", s_climbHeightPatch,
                kClimbHeightSites);
    removeSites("QoL climbing-speed hooks", s_climbingSpeedPatch,
                kClimbingSpeedSites);
}

static void publishSettings()
{
    setFloat(&g_tphdClimbingSpeedMultiplier, s_climbingSpeedMultiplier);
    setFloat(&g_tphdClimbHeightMultiplier, s_climbHeightMultiplier);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    setFloat(&g_tphdBlockPushSpeedMultiplier, s_blockPushSpeedMultiplier);
    setU32(&g_tphdBlockPushSpeedQuarters,
           (uint32_t)(s_blockPushSpeedMultiplier * 4.0f + 0.5f));
    const uint32_t bemosFrames = scaledDuration(
        13u, (uint32_t)(s_blockPushSpeedMultiplier * 4.0f + 0.5f));
    setFloat(&g_tphdBemosMovePhase, 32768.0f / (float)bemosFrames);
    setFloat(&g_tphdChainPullSpeedMultiplier, s_chainPullSpeedMultiplier);
    const uint32_t chainQuarters =
        (uint32_t)(s_chainPullSpeedMultiplier * 4.0f + 0.5f);
    if (g_tphdChainPullSpeedQuarters != chainQuarters)
        resetChandelierCurveStates();
    setU32(&g_tphdChainPullSpeedQuarters, chainQuarters);
#endif
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

    ImGui::SeparatorText("Movement");
    changed |= modifierSlider("Climbing Speed", &s_climbingSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ladders, vines/walls, ledge movement, and Wolf Link ledge climbing");

    changed |= modifierSlider("Climb Height", &s_climbHeightMultiplier,
                              0.25f, 4.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scale native ledge probe and grab heights for human and Wolf Link");

#ifdef TPHD_TOOLS_EXPERIMENTAL
    changed |= modifierSlider("Block Push/Pull Speed",
                              &s_blockPushSpeedMultiplier, 0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scales Link's animation and the block's travel time");

    changed |= modifierSlider("Chain Pull Speed",
                              &s_chainPullSpeedMultiplier, 0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scales human/Wolf animation and physical chain-pull movement");
#endif

    changed |= modifierSlider("Crawling Speed", &s_crawlSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Applies to human crawl and Wolf Link crawl/lie movement");

    changed |= modifierSlider("Roll Speed / Distance", &s_rollSpeedMultiplier,
                              0.25f, 5.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scale front/side roll animation rate and movement velocity");

    ImGui::SeparatorText("World & Time");
    WorldEditor::DrawMenuItem();

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

    ImGui::SeparatorText("Reset");
    if (ImGui::Button("Restore QoL Defaults")) {
        s_climbingSpeedMultiplier = 1.0f;
        s_climbHeightMultiplier = 1.0f;
#ifdef TPHD_TOOLS_EXPERIMENTAL
        s_blockPushSpeedMultiplier = 1.0f;
        s_chainPullSpeedMultiplier = 1.0f;
#endif
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
#ifdef TPHD_TOOLS_EXPERIMENTAL
    g_tphdQoLFrameSerial = g_tphdQoLFrameSerial + 1u;
#endif
    installPermanentHooks();
    publishSettings();
}

void OnApplicationStart()
{
#ifdef TPHD_TOOLS_EXPERIMENTAL
    g_tphdQoLFrameSerial = 0u;
    resetChandelierCurveStates();
#endif
    s_climbingSpeedPatch = {};
    s_climbHeightPatch = {};
#ifdef TPHD_TOOLS_EXPERIMENTAL
    s_blockPushSpeedPatch = {};
    s_chainPullSpeedPatch = {};
#endif
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
#ifdef TPHD_TOOLS_EXPERIMENTAL
    setFloat(&g_tphdBlockPushSpeedMultiplier, 1.0f);
    setU32(&g_tphdBlockPushSpeedQuarters, 4u);
    setFloat(&g_tphdBemosMovePhase, 32768.0f / 13.0f);
    setFloat(&g_tphdChainPullSpeedMultiplier, 1.0f);
    setU32(&g_tphdChainPullSpeedQuarters, 4u);
    resetChandelierCurveStates();
#endif
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

float GetChainPullSpeedMultiplier()
{
    return s_chainPullSpeedMultiplier;
}

void SetChainPullSpeedMultiplier(float value)
{
    s_chainPullSpeedMultiplier = clampQuarter(value, 0.25f, 5.0f);
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
