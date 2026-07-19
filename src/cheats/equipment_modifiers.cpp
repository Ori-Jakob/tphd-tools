// Native TPHD equipment modifiers.
//
// The addresses below were checked against the US Zelda.rpx in Ghidra and the
// original GC/Wii behavior in dusklight.  Code patches are installed by this
// RPL/WPS at runtime, validate their original instruction first, and restore it
// when disabled.  In particular, spinner speed and Super Clawshot do not edit
// the shared float pool used by unrelated Link actions.

#include "cheats/equipment_modifiers.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "imgui.h"
#include "input.h"
#include "logger.h"
#include "overlay.h"
#include "game/game.h"

namespace Cheats {
namespace EquipmentModifiers {

namespace {

struct PatchWord {
    u32 address;
    u32 expected;
    u32 replacement;
};

struct PatchOwner {
    bool applied;
    bool conflictReported;
};

enum ConfigItem {
    CONFIG_SUPER_CLAWSHOT = 0,
    CONFIG_INFINITE_SPINNER_TIME,
    CONFIG_SPINNER_SPEED_SUPER,
    CONFIG_SPINNER_SPEED_ULTRA,
    CONFIG_REMOTE_BOMBS,
    CONFIG_UNRESTRICTED_BOMBS,
    CONFIG_COUNT,
};

static const char* const kConfigNames[CONFIG_COUNT] = {
    "Super Clawshot",
    "Infinite Spinner Time",
    "Spinner Speed: Super",
    "Spinner Speed: Ultra",
    "Remote Bombs",
    "Unrestricted Bombs",
};

static bool s_config[CONFIG_COUNT] = {};

static PatchOwner s_clawshotPatch = {};
static PatchOwner s_spinnerTimePatch = {};
static PatchOwner s_spinnerSpeedPatch = {};
static PatchOwner s_remoteBombFusePatch = {};
static PatchOwner s_unrestrictedBombsPatch = {};

// These are deliberately RPL/WPS-owned values.  Patched native load sites read
// them directly, avoiding TPHD's deduplicated constant pool (for example,
// 0x10007480 is both a Clawshot value and the Arbiter's Grounds spinner speed).
static float s_clawshotShootSpeed = 500.0f;
static float s_clawshotRange = 32000.0f;
static float s_clawshotPullSpeed = 300.0f;
static float s_clawshotReturnSpeed = 500.0f;
static float s_spinnerSpeed = 50.0f;

static u32 makeLis(unsigned reg, const void* value)
{
    const u32 address = (u32)(uintptr_t)value;
    const u16 highAdjusted = (u16)((address + 0x8000u) >> 16);
    return 0x3C000000u | ((reg & 31u) << 21) | highAdjusted;
}

static u32 makeLfs(unsigned freg, unsigned baseReg, const void* value)
{
    const u32 address = (u32)(uintptr_t)value;
    return 0xC0000000u | ((freg & 31u) << 21) |
           ((baseReg & 31u) << 16) | (address & 0xFFFFu);
}

static void flushInstruction(volatile u32* p)
{
    DCFlushRange((void*)p, sizeof(*p));
    ICInvalidateRange((void*)p, sizeof(*p));
}

static void reportPatchConflict(const char* name, PatchOwner& owner,
                                const PatchWord& word, u32 found,
                                const char* operation)
{
    if (owner.conflictReported)
        return;
    Logger::LogWarn(
        "[tphd_tools][equipment] %s %s refused: "
        "%08X contains %08X (native %08X, ours %08X)",
        name, operation, (unsigned)word.address, (unsigned)found,
        (unsigned)word.expected, (unsigned)word.replacement);
    owner.conflictReported = true;
}

static bool applyPatch(const char* name, PatchOwner& owner,
                       const PatchWord* words, int count)
{
    // Reconcile against live text every frame rather than trusting only the
    // bookkeeping bit. This lets us recover if Zelda's text was refreshed or
    // the front-end lifecycle lost our local ownership state.
    bool needsWrite = false;

    for (int i = 0; i < count; ++i) {
        const volatile u32* p = (const volatile u32*)words[i].address;
        const u32 found = *p;
        if (found == words[i].expected) {
            needsWrite = true;
        } else if (found != words[i].replacement) {
            reportPatchConflict(name, owner, words[i], found, "enable");
            owner.applied = false;
            return false;
        }
    }

    if (needsWrite) {
        for (int i = 0; i < count; ++i) {
            volatile u32* p = (volatile u32*)words[i].address;
            if (*p == words[i].expected) {
                *p = words[i].replacement;
                flushInstruction(p);
            }
        }
        OSMemoryBarrier();
    }

    // Read back the complete patch. A failed cache/code write must not be
    // recorded as owned, otherwise the later disable path could claim success
    // without ever having restored native behavior.
    for (int i = 0; i < count; ++i) {
        const volatile u32* p = (const volatile u32*)words[i].address;
        const u32 found = *p;
        if (found != words[i].replacement) {
            reportPatchConflict(name, owner, words[i], found, "verify-enable");
            owner.applied = false;
            return false;
        }
    }

    owner.applied = true;
    owner.conflictReported = false;
    if (needsWrite)
        Logger::Log("[tphd_tools][equipment] %s enabled (%d words)",
                    name, count);
    return true;
}

static bool removePatch(const char* name, PatchOwner& owner,
                        const PatchWord* words, int count)
{
    bool conflict = false;
    bool wrote = false;

    // Inspect the live words even if owner.applied is false. A lifecycle reset
    // can lose that bit while Zelda's text still contains our instructions.
    // Only our exact replacement is reverted; an unrelated patch is preserved.
    for (int i = 0; i < count; ++i) {
        volatile u32* p = (volatile u32*)words[i].address;
        const u32 found = *p;
        if (found == words[i].replacement) {
            *p = words[i].expected;
            flushInstruction(p);
            wrote = true;
        } else if (found != words[i].expected) {
            reportPatchConflict(name, owner, words[i], found, "restore");
            conflict = true;
        }
    }
    if (wrote)
        OSMemoryBarrier();

    bool restored = !conflict;
    for (int i = 0; i < count; ++i) {
        const volatile u32* p = (const volatile u32*)words[i].address;
        if (*p != words[i].expected) {
            restored = false;
            if (*p == words[i].replacement)
                reportPatchConflict(name, owner, words[i], *p,
                                    "verify-restore");
        }
    }

    owner.applied = !restored;
    if (restored) {
        owner.conflictReported = false;
        if (wrote)
            Logger::Log("[tphd_tools][equipment] %s restored (%d words)",
                        name, count);
    }
    return restored;
}

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

static void setSuperClawshotPatch(bool on)
{
    // daAlink_c::checkHookshotStickBG plus every Clawshot-only load needed to
    // reproduce MegaCheats' Super preset without modifying its shared float
    // pool. Besides setHookshotPos's movement values, this includes the sight
    // range, Link's actual procHookshotFly pull speed, and the three collision
    // sweep backtracks that must grow with the outbound speed. The default
    // opcodes are retained as version/conflict guards.
    PatchWord words[] = {
        { 0x0202B12Cu, 0x38600000u, 0x38600001u },
        // Boss-room outbound speed (the native 150 shares the normal return
        // constant, but it is semantically the shoot speed in this branch).
        { 0x020592C4u, 0x3C801000u, makeLis(4, &s_clawshotShootSpeed) },
        { 0x020592D0u, 0xC3C473F0u, makeLfs(30, 4, &s_clawshotShootSpeed) },
        // Normal-room return, outbound speed, and maximum flight range.
        { 0x020592DCu, 0x3D801000u, makeLis(12, &s_clawshotReturnSpeed) },
        { 0x020592E4u, 0x3D601000u, makeLis(11, &s_clawshotShootSpeed) },
        { 0x020592E8u, 0xC3EC73F0u, makeLfs(31, 12, &s_clawshotReturnSpeed) },
        { 0x020592ECu, 0x3CE01000u, makeLis(7, &s_clawshotRange) },
        { 0x020592F0u, 0xC3CB73BCu, makeLfs(30, 11, &s_clawshotShootSpeed) },
        { 0x020592F8u, 0xC3A777A0u, makeLfs(29, 7, &s_clawshotRange) },
        // Return speed while retracting a hooked actor.
        { 0x02059328u, 0x3C801000u, makeLis(4, &s_clawshotPullSpeed) },
        { 0x0205932Cu, 0xC3E47480u, makeLfs(31, 4, &s_clawshotPullSpeed) },
        // procHookshotFly: actual Link movement toward a latched hook point.
        { 0x020423B4u, 0x3CA01000u, makeLis(5, &s_clawshotPullSpeed) },
        { 0x020423B8u, 0xC1A57480u, makeLfs(13, 5, &s_clawshotPullSpeed) },
        // setHookshotSight: keep targeting/lock range equal to flight range.
        { 0x0209CF7Cu, 0x3D601000u, makeLis(11, &s_clawshotRange) },
        { 0x0209CF84u, 0xC02B77A0u, makeLfs(1, 11, &s_clawshotRange) },
        // setHookshotPos collision sweeps. Native uses the same 100-unit
        // constant as shoot speed; all three paths must follow the new 500.
        { 0x02059C8Cu, 0x3D401000u, makeLis(10, &s_clawshotShootSpeed) },
        { 0x02059C94u, 0xC02A73BCu, makeLfs(1, 10, &s_clawshotShootSpeed) },
        { 0x02059D2Cu, 0x3D401000u, makeLis(10, &s_clawshotShootSpeed) },
        { 0x02059D34u, 0xC02A73BCu, makeLfs(1, 10, &s_clawshotShootSpeed) },
        { 0x02059DBCu, 0x3D401000u, makeLis(10, &s_clawshotShootSpeed) },
        { 0x02059DC4u, 0xC02A73BCu, makeLfs(1, 10, &s_clawshotShootSpeed) },
    };
    const int count = (int)(sizeof(words) / sizeof(words[0]));
    if (on)
        applyPatch("Super Clawshot", s_clawshotPatch, words, count);
    else
        removePatch("Super Clawshot", s_clawshotPatch, words, count);
}

static void setInfiniteSpinnerTimePatch(bool on)
{
    // daSpinner_c::execute: keep mRideMoveTime unchanged instead of subtracting
    // one.  This preserves the native timer value and affects no other decay.
    const PatchWord words[] = {
        { 0x0288D8ACu, 0x3967FFFFu, 0x7CEB3B78u }, // subi r11,r7,1 -> mr r11,r7
    };
    if (on)
        applyPatch("Infinite Spinner Time", s_spinnerTimePatch, words, 1);
    else
        removePatch("Infinite Spinner Time", s_spinnerTimePatch, words, 1);
}

static void setSpinnerSpeedPatch(int mode)
{
    PatchWord words[] = {
        // daAlink_c::getSpinnerRideSpeedF -> load the selected RPL value.
        { 0x020B30F4u, 0x7C0802A6u, makeLis(12, &s_spinnerSpeed) },
        { 0x020B30F8u, 0x90010004u, makeLfs(1, 12, &s_spinnerSpeed) },
        { 0x020B30FCu, 0x9421FFF8u, 0x4E800020u },
    };
    const int count = (int)(sizeof(words) / sizeof(words[0]));

    if (mode == 0) {
        removePatch("Spinner Speed", s_spinnerSpeedPatch, words, count);
        return;
    }

    s_spinnerSpeed = mode == 2 ? 100.0f : 50.0f;
    DCFlushRange(&s_spinnerSpeed, sizeof(s_spinnerSpeed));
    applyPatch("Spinner Speed", s_spinnerSpeedPatch, words, count);
}

static void setRemoteBombFusePatch(bool on)
{
    // daNbomb_c::checkExplode: stop the native mExTime countdown. BOMB_HIT is
    // still tested later in the same function, so the actor's normal explosion
    // path remains available to the configurable remote trigger below.
    const PatchWord words[] = {
        { 0x0248802Cu, 0x3BDEFFFFu, 0x60000000u }, // subi r30,r30,1 -> nop
    };
    if (on)
        applyPatch("Remote Bombs fuse", s_remoteBombFusePatch, words, 1);
    else
        removePatch("Remote Bombs fuse", s_remoteBombFusePatch, words, 1);
}

static void setUnrestrictedBombsPatch(bool on)
{
    // daAlink_c::checkNewItemChange: after confirming item 0x70/0x71, remove
    // only the active-bomb-count >= 3 rejection.  The live count remains valid.
    const PatchWord words[] = {
        { 0x02050C5Cu, 0x4080F698u, 0x60000000u }, // bge reject -> nop
    };
    if (on)
        applyPatch("Unrestricted Bombs", s_unrestrictedBombsPatch, words, 1);
    else
        removePatch("Unrestricted Bombs", s_unrestrictedBombsPatch, words, 1);
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

    changed |= ImGui::Checkbox("Super Clawshot",
                               &s_config[CONFIG_SUPER_CLAWSHOT]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Longer, faster Clawshot that sticks to any valid background");

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

    changed |= ImGui::Checkbox("Remote Bombs",
                               &s_config[CONFIG_REMOTE_BOMBS]);
    if (ImGui::IsItemHovered()) {
        char hotkey[64];
        Input::HotkeyToString(ov::g_settings.remoteBombsCombo,
                              hotkey, sizeof(hotkey));
        ImGui::SetTooltip("Press %s to detonate player-created bombs; timed fuses stay frozen",
                          hotkey);
    }

    changed |= ImGui::Checkbox("Unrestricted Bombs",
                               &s_config[CONFIG_UNRESTRICTED_BOMBS]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove the three-active-bomb limit (use Infinite Ammo to refill bags)");

    ImGui::Separator();
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
    setSuperClawshotPatch(s_config[CONFIG_SUPER_CLAWSHOT]);
    setInfiniteSpinnerTimePatch(s_config[CONFIG_INFINITE_SPINNER_TIME]);
    setSpinnerSpeedPatch(spinnerSpeedMode());
    setRemoteBombFusePatch(s_config[CONFIG_REMOTE_BOMBS]);
    setUnrestrictedBombsPatch(s_config[CONFIG_UNRESTRICTED_BOMBS]);
    tickRemoteBombs(s_config[CONFIG_REMOTE_BOMBS]);
}

void OnApplicationStart()
{
    // A new Zelda.rpx owns fresh text at the same fixed addresses.  Do not try
    // to restore the previous process's memory; simply relinquish ownership so
    // the first Tick validates and installs enabled patches again.
    s_clawshotPatch = {};
    s_spinnerTimePatch = {};
    s_spinnerSpeedPatch = {};
    s_remoteBombFusePatch = {};
    s_unrestrictedBombsPatch = {};
}

void OnApplicationEnd()
{
    setSuperClawshotPatch(false);
    setInfiniteSpinnerTimePatch(false);
    setSpinnerSpeedPatch(0);
    setRemoteBombFusePatch(false);
    setUnrestrictedBombsPatch(false);
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
