// Native TPHD equipment modifiers.
//
// The addresses below were checked against the US Zelda.rpx in Ghidra and the
// original GC/Wii behavior in dusklight. The executable redirects are installed
// once, before gameplay code is translated, and remain stable for the title
// lifetime. Checkboxes change only RPL/WPS-owned data read by those redirects.
// This is important on Cemu: coreinit's ICInvalidateRange only invalidates JIT
// blocks in the OS codegen range, not already-translated Zelda.rpx text.

#include "cheats/equipment_modifiers.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "imgui.h"
#include "code_patch.h"
#include "input.h"
#include "logger.h"
#include "overlay.h"
#include "ui_hotkey.h"
#include "game/game.h"

// Read directly by the small PowerPC thunks in equipment_modifiers_hooks.s.
// Keep C linkage so the assembly relocations have stable symbol names.
extern "C" {
volatile uint8_t g_tphdSuperClawshotEnabled = 0;
volatile uint8_t g_tphdInfiniteSpinnerTimeEnabled = 0;
volatile uint8_t g_tphdRemoteBombsEnabled = 0;
volatile uint8_t g_tphdUnrestrictedBombsEnabled = 0;
volatile uint8_t g_tphdUnrestrictedItemsEnabled = 0;
volatile uint8_t g_tphdFastIronBootsEnabled = 0;
float g_tphdFastIronBootsMultiplier = 1.0f;

void tphdSuperClawshotFailureHook();
void tphdInfiniteSpinnerTimeHook();
void tphdRemoteBombFuseHook();
void tphdUnrestrictedBombsHook();
void tphdUnrestrictedItemsHook();
void tphdUnrestrictedSwordHook();
void tphdFastIronBootsAnimeHook();
void tphdFastIronBootsStoreF13Hook();
void tphdFastIronBootsStoreF30Hook();
}

namespace Cheats {
namespace EquipmentModifiers {

namespace {

using PatchWord = CodePatch::Word;
using PatchOwner = CodePatch::Owner;

enum ConfigItem {
    CONFIG_SUPER_CLAWSHOT = 0,
    CONFIG_INFINITE_SPINNER_TIME,
    CONFIG_SPINNER_SPEED_SUPER,
    CONFIG_SPINNER_SPEED_ULTRA,
    CONFIG_REMOTE_BOMBS,
    CONFIG_UNRESTRICTED_BOMBS,
    CONFIG_FAST_IRON_BOOTS,
    CONFIG_COUNT,
};

static const char* const kConfigNames[CONFIG_COUNT] = {
    "Super Clawshot",
    "Infinite Spinner Time",
    "Spinner Speed: Super",
    "Spinner Speed: Ultra",
    "Remote Bombs",
    "No Bomb Limit",
    "Fast Iron Boots",
};

static bool s_config[CONFIG_COUNT] = {};

static PatchOwner s_clawshotPatch = {};
static PatchOwner s_spinnerTimePatch = {};
static PatchOwner s_spinnerSpeedPatch = {};
static PatchOwner s_remoteBombFusePatch = {};
static PatchOwner s_unrestrictedBombsPatch = {};
static PatchOwner s_unrestrictedItemsPatch = {};
static PatchOwner s_unrestrictedSwordPatch = {};
static PatchOwner s_fastIronBootsAnimePatch = {};
static PatchOwner s_fastIronBootsSpeedPatch = {};

// These are deliberately RPL/WPS-owned values. Permanently redirected native
// load sites read them directly, avoiding TPHD's heavily deduplicated constant
// pool (0x100073BC alone has more than sixty unrelated readers).
static float s_clawshotBossShootSpeed = 150.0f;
static float s_clawshotShootSpeed = 100.0f;
static float s_clawshotRange = 2000.0f;
static float s_clawshotPullSpeed = 60.0f;
static float s_clawshotReturnSpeed = 150.0f;
static float s_spinnerSpeed = 26.0f;

static int spinnerSpeedMode()
{
    if (s_config[CONFIG_SPINNER_SPEED_ULTRA])
        return 2;
    if (s_config[CONFIG_SPINNER_SPEED_SUPER])
        return 1;
    return 0;
}

static void setSpinnerSpeedMode(int mode)
{
    s_config[CONFIG_SPINNER_SPEED_SUPER] = mode == 1;
    s_config[CONFIG_SPINNER_SPEED_ULTRA] = mode == 2;
}

static bool buildSuperClawshotPatch(PatchWord* words, int* count)
{
    u32 failureHook = 0;
    if (!CodePatch::MakeHookBranch(
            "Super Clawshot", s_clawshotPatch, 0x0202B12Cu,
            (const void*)tphdSuperClawshotFailureHook, &failureHook))
        return false;

    // The failure return becomes a permanent call to a one-byte mode flag.
    // Every speed/range load is likewise redirected to private data. Native
    // values live in that data while disabled, so toggling never rewrites an
    // already-translated Zelda.rpx instruction.
    const PatchWord built[] = {
        { 0x0202B12Cu, 0x38600000u, failureHook },
        // Boss-room outbound speed is 150 natively, not the normal 100.
        { 0x020592C4u, 0x3C801000u, CodePatch::MakeLis(4, &s_clawshotBossShootSpeed) },
        { 0x020592D0u, 0xC3C473F0u, CodePatch::MakeLfs(30, 4, &s_clawshotBossShootSpeed) },
        // Normal-room return, outbound speed, and maximum flight range.
        { 0x020592DCu, 0x3D801000u, CodePatch::MakeLis(12, &s_clawshotReturnSpeed) },
        { 0x020592E4u, 0x3D601000u, CodePatch::MakeLis(11, &s_clawshotShootSpeed) },
        { 0x020592E8u, 0xC3EC73F0u, CodePatch::MakeLfs(31, 12, &s_clawshotReturnSpeed) },
        { 0x020592ECu, 0x3CE01000u, CodePatch::MakeLis(7, &s_clawshotRange) },
        { 0x020592F0u, 0xC3CB73BCu, CodePatch::MakeLfs(30, 11, &s_clawshotShootSpeed) },
        { 0x020592F8u, 0xC3A777A0u, CodePatch::MakeLfs(29, 7, &s_clawshotRange) },
        // Return speed while retracting a hooked actor.
        { 0x02059328u, 0x3C801000u, CodePatch::MakeLis(4, &s_clawshotPullSpeed) },
        { 0x0205932Cu, 0xC3E47480u, CodePatch::MakeLfs(31, 4, &s_clawshotPullSpeed) },
        // procHookshotFly: Link movement toward the latched point.
        { 0x020423B4u, 0x3CA01000u, CodePatch::MakeLis(5, &s_clawshotPullSpeed) },
        { 0x020423B8u, 0xC1A57480u, CodePatch::MakeLfs(13, 5, &s_clawshotPullSpeed) },
        // Normal-room targeting/lock range.
        { 0x0209CF7Cu, 0x3D601000u, CodePatch::MakeLis(11, &s_clawshotRange) },
        { 0x0209CF84u, 0xC02B77A0u, CodePatch::MakeLfs(1, 11, &s_clawshotRange) },
        // setHookshotPos collision sweeps use normal outbound speed.
        { 0x02059C8Cu, 0x3D401000u, CodePatch::MakeLis(10, &s_clawshotShootSpeed) },
        { 0x02059C94u, 0xC02A73BCu, CodePatch::MakeLfs(1, 10, &s_clawshotShootSpeed) },
        { 0x02059D2Cu, 0x3D401000u, CodePatch::MakeLis(10, &s_clawshotShootSpeed) },
        { 0x02059D34u, 0xC02A73BCu, CodePatch::MakeLfs(1, 10, &s_clawshotShootSpeed) },
        { 0x02059DBCu, 0x3D401000u, CodePatch::MakeLis(10, &s_clawshotShootSpeed) },
        { 0x02059DC4u, 0xC02A73BCu, CodePatch::MakeLfs(1, 10, &s_clawshotShootSpeed) },
    };
    *count = (int)(sizeof(built) / sizeof(built[0]));
    for (int i = 0; i < *count; ++i)
        words[i] = built[i];
    return true;
}

static bool buildUnrestrictedItemsPatch(PatchWord* words, int* count)
{
    u32 failureHook = 0;
    if (!CodePatch::MakeHookBranch(
            "Unrestricted Items", s_unrestrictedItemsPatch, 0x0201B304u,
            (const void*)tphdUnrestrictedItemsHook, &failureHook))
        return false;

    // checkCastleTownUseItem's restricted return restores LR/stack before its
    // final `li r3,0`. Insert the mode call before that epilogue and shift the
    // four restore instructions down one word. The hook supplies r3=0 while
    // disabled and r3=1 while enabled; native success paths are untouched.
    const PatchWord built[] = {
        { 0x0201B304u, 0x80010014u, failureHook }, // lwz r0,0x14(r1) -> bl hook
        { 0x0201B308u, 0x83E1000Cu, 0x80010014u },
        { 0x0201B30Cu, 0x7C0803A6u, 0x83E1000Cu },
        { 0x0201B310u, 0x38210010u, 0x7C0803A6u },
        { 0x0201B314u, 0x38600000u, 0x38210010u },
    };
    *count = (int)(sizeof(built) / sizeof(built[0]));
    for (int i = 0; i < *count; ++i)
        words[i] = built[i];
    return true;
}

static bool buildFastIronBootsAnimePatch(PatchWord* words, int* count)
{
    const struct Site {
        u32 address;
        u32 expected;
    } sites[] = {
        // setBlendMoveAnime: heavy forward-movement animation checks.
        { 0x02023648u, 0x4BFFF591u },
        { 0x02023884u, 0x4BFFF355u },
        // setBlendAtnBackMoveAnime: heavy target/back-walk animation check.
        { 0x0202BDDCu, 0x4BFF6DFDu },
    };

    *count = (int)(sizeof(sites) / sizeof(sites[0]));
    for (int i = 0; i < *count; ++i) {
        u32 branch = 0;
        if (!CodePatch::MakeHookBranch(
                "Fast Iron Boots animation", s_fastIronBootsAnimePatch,
                sites[i].address, (const void*)tphdFastIronBootsAnimeHook,
                &branch))
            return false;
        words[i] = { sites[i].address, sites[i].expected, branch };
    }
    return true;
}

static bool buildFastIronBootsSpeedPatch(PatchWord* words, int* count)
{
    const struct Site {
        u32 address;
        u32 expected;
        const void* hook;
    } sites[] = {
        // daAlink_c's input handler assigns mHeavySpeedMultiplier from several
        // grounded/underwater/heavy-state paths. Redirect every assignment so
        // the setting cannot be overwritten later in the same update.
        { 0x020639D8u, 0xD1BE34F4u,
          (const void*)tphdFastIronBootsStoreF13Hook },
        { 0x020639F0u, 0xD1BE34F4u,
          (const void*)tphdFastIronBootsStoreF13Hook },
        { 0x02063A34u, 0xD1BE34F4u,
          (const void*)tphdFastIronBootsStoreF13Hook },
        { 0x02063A44u, 0xD3DE34F4u,
          (const void*)tphdFastIronBootsStoreF30Hook },
        // 0x02063A98 is the separate wolf-underwater slowdown. Dusklight does
        // not include that state in Fast Iron Boots, so leave it native.
    };

    *count = (int)(sizeof(sites) / sizeof(sites[0]));
    for (int i = 0; i < *count; ++i) {
        u32 branch = 0;
        if (!CodePatch::MakeHookBranch(
                "Fast Iron Boots speed", s_fastIronBootsSpeedPatch,
                sites[i].address, sites[i].hook, &branch))
            return false;
        words[i] = { sites[i].address, sites[i].expected, branch };
    }
    return true;
}

static void installPermanentHooks()
{
    PatchWord clawshot[21];
    int clawshotCount = 0;
    if (buildSuperClawshotPatch(clawshot, &clawshotCount))
        CodePatch::Apply("Super Clawshot hook", s_clawshotPatch,
                         clawshot, clawshotCount);

    PatchWord word = {};
    if (CodePatch::BuildSingleHook(
            "Infinite Spinner Time", s_spinnerTimePatch,
            0x0288D8ACu, 0x3967FFFFu,
            (const void*)tphdInfiniteSpinnerTimeHook, &word))
        CodePatch::Apply("Infinite Spinner Time hook",
                         s_spinnerTimePatch, &word, 1);

    // Preserve getSpinnerRideSpeedF's native special 60-unit branch. Only its
    // ordinary 26-unit return is redirected to the selectable private value.
    const PatchWord spinnerSpeed[] = {
        { 0x020B3124u, 0x3D801000u, CodePatch::MakeLis(12, &s_spinnerSpeed) },
        { 0x020B3128u, 0xC02C751Cu, CodePatch::MakeLfs(1, 12, &s_spinnerSpeed) },
    };
    CodePatch::Apply("Spinner Speed hook", s_spinnerSpeedPatch,
                     spinnerSpeed, 2);

    if (CodePatch::BuildSingleHook(
            "Remote Bombs fuse", s_remoteBombFusePatch,
            0x0248802Cu, 0x3BDEFFFFu,
            (const void*)tphdRemoteBombFuseHook, &word))
        CodePatch::Apply("Remote Bombs fuse hook",
                         s_remoteBombFusePatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "No Bomb Limit", s_unrestrictedBombsPatch,
            0x02050C5Cu, 0x4080F698u,
            (const void*)tphdUnrestrictedBombsHook, &word))
        CodePatch::Apply("No Bomb Limit hook", s_unrestrictedBombsPatch,
                         &word, 1);

    PatchWord unrestrictedItems[5];
    int unrestrictedItemsCount = 0;
    if (buildUnrestrictedItemsPatch(unrestrictedItems,
                                    &unrestrictedItemsCount))
        CodePatch::Apply("Unrestricted Items hook", s_unrestrictedItemsPatch,
                         unrestrictedItems, unrestrictedItemsCount);

    // Sword drawing has an additional checkNotBattleStage call that the item
    // restriction function never sees. Hook only this call site; other users
    // of checkNotBattleStage retain their native behavior.
    if (CodePatch::BuildSingleHook(
            "Unrestricted Items sword", s_unrestrictedSwordPatch,
            0x02050EB0u, 0x4BFCA355u,
            (const void*)tphdUnrestrictedSwordHook, &word))
        CodePatch::Apply("Unrestricted Items sword hook",
                         s_unrestrictedSwordPatch, &word, 1);

    PatchWord fastBootsAnime[3];
    int fastBootsAnimeCount = 0;
    if (buildFastIronBootsAnimePatch(fastBootsAnime,
                                     &fastBootsAnimeCount))
        CodePatch::Apply("Fast Iron Boots animation hook",
                         s_fastIronBootsAnimePatch, fastBootsAnime,
                         fastBootsAnimeCount);

    PatchWord fastBootsSpeed[4];
    int fastBootsSpeedCount = 0;
    if (buildFastIronBootsSpeedPatch(fastBootsSpeed,
                                     &fastBootsSpeedCount))
        CodePatch::Apply("Fast Iron Boots speed hook",
                         s_fastIronBootsSpeedPatch, fastBootsSpeed,
                         fastBootsSpeedCount);
}

static void removePermanentHooks()
{
    PatchWord clawshot[21];
    int clawshotCount = 0;
    if (buildSuperClawshotPatch(clawshot, &clawshotCount))
        CodePatch::Remove("Super Clawshot hook", s_clawshotPatch,
                          clawshot, clawshotCount);

    PatchWord word = {};
    if (CodePatch::BuildSingleHook(
            "Infinite Spinner Time", s_spinnerTimePatch,
            0x0288D8ACu, 0x3967FFFFu,
            (const void*)tphdInfiniteSpinnerTimeHook, &word))
        CodePatch::Remove("Infinite Spinner Time hook",
                          s_spinnerTimePatch, &word, 1);

    const PatchWord spinnerSpeed[] = {
        { 0x020B3124u, 0x3D801000u, CodePatch::MakeLis(12, &s_spinnerSpeed) },
        { 0x020B3128u, 0xC02C751Cu, CodePatch::MakeLfs(1, 12, &s_spinnerSpeed) },
    };
    CodePatch::Remove("Spinner Speed hook", s_spinnerSpeedPatch,
                      spinnerSpeed, 2);

    if (CodePatch::BuildSingleHook(
            "Remote Bombs fuse", s_remoteBombFusePatch,
            0x0248802Cu, 0x3BDEFFFFu,
            (const void*)tphdRemoteBombFuseHook, &word))
        CodePatch::Remove("Remote Bombs fuse hook",
                          s_remoteBombFusePatch, &word, 1);

    if (CodePatch::BuildSingleHook(
            "No Bomb Limit", s_unrestrictedBombsPatch,
            0x02050C5Cu, 0x4080F698u,
            (const void*)tphdUnrestrictedBombsHook, &word))
        CodePatch::Remove("No Bomb Limit hook", s_unrestrictedBombsPatch,
                          &word, 1);

    PatchWord unrestrictedItems[5];
    int unrestrictedItemsCount = 0;
    if (buildUnrestrictedItemsPatch(unrestrictedItems,
                                    &unrestrictedItemsCount))
        CodePatch::Remove("Unrestricted Items hook", s_unrestrictedItemsPatch,
                          unrestrictedItems, unrestrictedItemsCount);

    if (CodePatch::BuildSingleHook(
            "Unrestricted Items sword", s_unrestrictedSwordPatch,
            0x02050EB0u, 0x4BFCA355u,
            (const void*)tphdUnrestrictedSwordHook, &word))
        CodePatch::Remove("Unrestricted Items sword hook",
                          s_unrestrictedSwordPatch, &word, 1);

    PatchWord fastBootsAnime[3];
    int fastBootsAnimeCount = 0;
    if (buildFastIronBootsAnimePatch(fastBootsAnime,
                                     &fastBootsAnimeCount))
        CodePatch::Remove("Fast Iron Boots animation hook",
                          s_fastIronBootsAnimePatch, fastBootsAnime,
                          fastBootsAnimeCount);

    PatchWord fastBootsSpeed[4];
    int fastBootsSpeedCount = 0;
    if (buildFastIronBootsSpeedPatch(fastBootsSpeed,
                                     &fastBootsSpeedCount))
        CodePatch::Remove("Fast Iron Boots speed hook",
                          s_fastIronBootsSpeedPatch, fastBootsSpeed,
                          fastBootsSpeedCount);
}

static bool setByteMode(volatile uint8_t* value, bool on)
{
    const uint8_t next = on ? 1 : 0;
    if (*value == next)
        return false;
    *value = next;
    DCFlushRange((void*)value, sizeof(*value));
    OSMemoryBarrier();
    return true;
}

static void setSuperClawshotMode(bool on)
{
    const float bossShoot = on ? 500.0f : 150.0f;
    const float shoot = on ? 500.0f : 100.0f;
    const float range = on ? 32000.0f : 2000.0f;
    const float pull = on ? 300.0f : 60.0f;
    const float ret = on ? 500.0f : 150.0f;
    const bool valuesChanged = s_clawshotBossShootSpeed != bossShoot ||
                               s_clawshotShootSpeed != shoot ||
                               s_clawshotRange != range ||
                               s_clawshotPullSpeed != pull ||
                               s_clawshotReturnSpeed != ret;
    if (valuesChanged) {
        s_clawshotBossShootSpeed = bossShoot;
        s_clawshotShootSpeed = shoot;
        s_clawshotRange = range;
        s_clawshotPullSpeed = pull;
        s_clawshotReturnSpeed = ret;
        DCFlushRange(&s_clawshotBossShootSpeed,
                     sizeof(s_clawshotBossShootSpeed));
        DCFlushRange(&s_clawshotShootSpeed, sizeof(s_clawshotShootSpeed));
        DCFlushRange(&s_clawshotRange, sizeof(s_clawshotRange));
        DCFlushRange(&s_clawshotPullSpeed, sizeof(s_clawshotPullSpeed));
        DCFlushRange(&s_clawshotReturnSpeed, sizeof(s_clawshotReturnSpeed));
        OSMemoryBarrier();
    }
    // Publish the behavior bit only after all associated values are coherent.
    if (setByteMode(&g_tphdSuperClawshotEnabled, on)) {
        Logger::Log(
            "[tphd_tools][equipment] Super Clawshot mode %s "
            "(shoot=%.0f range=%.0f pull=%.0f return=%.0f)",
            on ? "enabled" : "disabled", (double)shoot, (double)range,
            (double)pull, (double)ret);
    }
}

static void setSpinnerSpeedValue(int mode)
{
    const float speed = mode == 2 ? 100.0f : mode == 1 ? 50.0f : 26.0f;
    if (s_spinnerSpeed == speed)
        return;
    s_spinnerSpeed = speed;
    DCFlushRange(&s_spinnerSpeed, sizeof(s_spinnerSpeed));
    OSMemoryBarrier();
    Logger::Log("[tphd_tools][equipment] Spinner Speed mode=%d value=%.0f",
                mode, (double)speed);
}

static fopAc_ac_c* detonatePlayerBomb(fopAc_ac_c* actor, void*)
{
    if (!actor || fopAcM_GetName(actor) != FPCNM_NBOMB)
        return nullptr;

    // TPHD daNbomb_c gained a four-byte vtable/layout slot relative to GC:
    // mStateFlg0 is +0xB50.  PLAYER_MAKE filters out flowers/enemy bombs;
    // BOMB_HIT enters the actor's normal native explosion path next update.
    volatile u32* state = (volatile u32*)((u8*)actor + 0xB50u);
    if ((*state & 0x1u) != 0)
        *state |= 0x4u;
    return nullptr;
}

static void tickRemoteBombs(bool enabled)
{
    if (!enabled)
        return;

    Input::Snapshot input = {};
    Input::GetSnapshot(&input);
    const u32 combo = ov::g_settings.remoteBombsCombo;
    const bool comboDown = !ov::g_menuVisible && combo != 0 &&
                           input.buttons == combo;
    // Require a fresh edge from at least one combo button. This also prevents
    // a combo held while closing the menu from leaking into gameplay. Exact
    // matching keeps the default ZR+L3 from firing inside Fly Cam's larger
    // default ZL+ZR+L3+R3 combo.
    if (comboDown && (input.pressed & combo) != 0 && dComIfGp_getPlayer())
        fopAcIt_Judge(detonatePlayerBomb, nullptr);
}

} // namespace

void DrawMenu()
{
    bool changed = false;

    ImGui::SeparatorText("Clawshot");
    changed |= ImGui::Checkbox("Super Clawshot",
                               &s_config[CONFIG_SUPER_CLAWSHOT]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Longer, faster Clawshot that sticks to any valid background");

    ImGui::SeparatorText("Spinner");
    changed |= ImGui::Checkbox("Infinite Spinner Time",
                               &s_config[CONFIG_INFINITE_SPINNER_TIME]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop the Spinner's native ride timer from counting down");

    int speed = spinnerSpeedMode();
    const char* speedNames[] = { "Default (26)", "Super (50)", "Ultra (100)" };
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("Spinner Speed", &speed, speedNames,
                     (int)(sizeof(speedNames) / sizeof(speedNames[0])))) {
        setSpinnerSpeedMode(speed);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Changes only the Spinner ride-speed accessor; Link's jump stays normal");

    ImGui::SeparatorText("Bombs");
    changed |= ImGui::Checkbox("Remote Bombs",
                               &s_config[CONFIG_REMOTE_BOMBS]);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        UiHotkey::DrawText(ov::g_settings.remoteBombsCombo, "Press ",
                           " to detonate player-created bombs; timed fuses stay frozen");
        ImGui::EndTooltip();
    }

    changed |= ImGui::Checkbox("No Bomb Limit",
                               &s_config[CONFIG_UNRESTRICTED_BOMBS]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove the three-active-bomb limit (use Infinite Ammo to refill bags)");

    ImGui::SeparatorText("Movement Gear");
    changed |= ImGui::Checkbox("Fast Iron Boots",
                               &s_config[CONFIG_FAST_IRON_BOOTS]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use normal movement speed and animations while heavy");

    ImGui::SeparatorText("Reset");
    if (ImGui::Button("Restore All Defaults")) {
        for (int i = 0; i < CONFIG_COUNT; ++i)
            s_config[i] = false;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Disable every equipment modifier, including Spinner Speed");

    // Cheats::Tick ran before this frame's ImGui draw. Reconcile immediately
    // on a UI change so an unchecked patch is removed before gameplay resumes,
    // rather than relying on a later present callback.
    if (changed)
        Tick();
}

void Tick()
{
    // Hooks stay installed for the title lifetime; only ordinary data changes
    // here. This makes both enable and disable visible to Cemu immediately.
    installPermanentHooks();
    setSuperClawshotMode(s_config[CONFIG_SUPER_CLAWSHOT]);
    if (setByteMode(&g_tphdInfiniteSpinnerTimeEnabled,
                    s_config[CONFIG_INFINITE_SPINNER_TIME]))
        Logger::Log("[tphd_tools][equipment] Infinite Spinner Time mode %s",
                    s_config[CONFIG_INFINITE_SPINNER_TIME] ? "enabled" :
                                                            "disabled");
    setSpinnerSpeedValue(spinnerSpeedMode());
    if (setByteMode(&g_tphdRemoteBombsEnabled,
                    s_config[CONFIG_REMOTE_BOMBS]))
        Logger::Log("[tphd_tools][equipment] Remote Bombs mode %s",
                    s_config[CONFIG_REMOTE_BOMBS] ? "enabled" : "disabled");
    if (setByteMode(&g_tphdUnrestrictedBombsEnabled,
                    s_config[CONFIG_UNRESTRICTED_BOMBS]))
        Logger::Log("[tphd_tools][equipment] No Bomb Limit mode %s",
                    s_config[CONFIG_UNRESTRICTED_BOMBS] ? "enabled" :
                                                        "disabled");
    if (setByteMode(&g_tphdFastIronBootsEnabled,
                    s_config[CONFIG_FAST_IRON_BOOTS]))
        Logger::Log("[tphd_tools][equipment] Fast Iron Boots mode %s",
                    s_config[CONFIG_FAST_IRON_BOOTS] ? "enabled" :
                                                     "disabled");
    tickRemoteBombs(s_config[CONFIG_REMOTE_BOMBS]);
}

void OnApplicationStart()
{
    // Install before gameplay functions can be JIT-translated. The redirects
    // initially expose native values (or the settings retained by Aroma), then
    // Config::Load can update the ordinary data on the first present.
    s_clawshotPatch = {};
    s_spinnerTimePatch = {};
    s_spinnerSpeedPatch = {};
    s_remoteBombFusePatch = {};
    s_unrestrictedBombsPatch = {};
    s_unrestrictedItemsPatch = {};
    s_unrestrictedSwordPatch = {};
    s_fastIronBootsAnimePatch = {};
    s_fastIronBootsSpeedPatch = {};
    setSuperClawshotMode(s_config[CONFIG_SUPER_CLAWSHOT]);
    setByteMode(&g_tphdInfiniteSpinnerTimeEnabled,
                s_config[CONFIG_INFINITE_SPINNER_TIME]);
    setSpinnerSpeedValue(spinnerSpeedMode());
    setByteMode(&g_tphdRemoteBombsEnabled,
                s_config[CONFIG_REMOTE_BOMBS]);
    setByteMode(&g_tphdUnrestrictedBombsEnabled,
                s_config[CONFIG_UNRESTRICTED_BOMBS]);
    setByteMode(&g_tphdFastIronBootsEnabled,
                s_config[CONFIG_FAST_IRON_BOOTS]);
    installPermanentHooks();
}

void OnApplicationEnd()
{
    setSuperClawshotMode(false);
    setByteMode(&g_tphdInfiniteSpinnerTimeEnabled, false);
    setSpinnerSpeedValue(0);
    setByteMode(&g_tphdRemoteBombsEnabled, false);
    setByteMode(&g_tphdUnrestrictedBombsEnabled, false);
    setByteMode(&g_tphdUnrestrictedItemsEnabled, false);
    setByteMode(&g_tphdFastIronBootsEnabled, false);
    removePermanentHooks();
}

void SetUnrestrictedItemsEnabled(bool on)
{
    if (setByteMode(&g_tphdUnrestrictedItemsEnabled, on))
        Logger::Log("[tphd_tools][equipment] Unrestricted Items mode %s",
                    on ? "enabled" : "disabled");
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
    if (i < 0 || i >= CONFIG_COUNT)
        return;
    s_config[i] = on;
    // A malformed/hand-edited config cannot leave both speed modes selected.
    if (on && i == CONFIG_SPINNER_SPEED_SUPER)
        s_config[CONFIG_SPINNER_SPEED_ULTRA] = false;
    else if (on && i == CONFIG_SPINNER_SPEED_ULTRA)
        s_config[CONFIG_SPINNER_SPEED_SUPER] = false;
}

} // namespace EquipmentModifiers
} // namespace Cheats
