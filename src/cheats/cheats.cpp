// cheats/cheats.cpp -- see cheats.h.
//
// Registry of tpgz-style cheats. To add one:
//   1. Write a `static void cheat_xxx() { ... }` that pokes game state (use the
//      game/ bindings; add new structs/addresses to include/game/ as needed).
//   2. Add a row to s_cheats below: { "Name", "Tooltip", false, cheat_xxx }.
// `tick` runs every frame while the cheat's checkbox is on. Leave `tick` null
// for cheats that only need a one-shot button (draw that in the gameplay menu).
#include "cheats/cheats.h"
#include "cheats/equipment_modifiers.h"
#include "cheats/difficulty.h"
#include "cheats/qol.h"
#include "cheats/inventory_editor.h"
#include "input.h"
#include "overlay.h"

#include "imgui.h"
#include "game/game.h"

namespace Cheats {

struct Cheat {
    const char* name;
    const char* desc;
    bool        enabled;
    void      (*tick)();
};

// --- cheat implementations (one at a time, ported from tpgz / MegaCheats) ---

// Moon Jump: hold the rebindable combo (default ZR+A) to drive Link straight
// up. n0ted's MegaCheats writes the gravity value into Link's Y velocity
// (actor+0x4FC = speed.y) while the combo is held; we do the same from the
// present hook.
static void cheat_moonJump()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    GameInput in;
    if (!dCam_getInput(&in))
        return;
    u32 b = dCam_normalizeButtons(in.buttons, in.type);
    u32 combo = ov::MenuButtonsToPro(ov::g_settings.moonJumpCombo);
    if (combo != 0 && (b & combo) == combo)
        link->speed.y = 55.0f;
}

// Infinite Hearts: keep the live current life full ((maxLife/5)*4). Writes the
// HUD/gameplay meter copy (g_meterInfo), not the persistent save block.
static void cheat_infiniteHearts()
{
    if (g_meterInfo->mMaxLife)
        g_meterInfo->mLife = (u16)((g_meterInfo->mMaxLife / 5) * 4);
}

// Infinite Rupees: keep the live rupee count at the equipped wallet's native
// limit. dSv_player_status_a_c::getRupeeMax selects 500/1000/2000/9999 from
// the TPHD table at 0x1009F6EC.
static void cheat_infiniteRupees()
{
    const u16 max = dSv_getRupeeMax((const dSv_status_a_c*)g_svStatusA);
    if (max != 0)
        g_meterInfo->mRupee = max;
}

// Infinite Air: keep Link's oxygen full while underwater.
static void cheat_infiniteAir()
{
    *dComIfGp_getOxygen() = 600;
}

// Quick Transform: press ZR+Y to swap human<->wolf without an owl statue / Midna
// menu (tpgz / MegaCheats). The combo's rising edge + the Y-button takeover are
// detected in the input hooks (Input::QuickTransformFired) so the game can't
// equip the Y-item first -- the transform takes precedence. The engine guards
// (Ball & Chain, checkMetamorphoseEnableBase) still play an error SE if it can't
// transform here. The proc change takes effect on Link's next update.
static void cheat_quickTransform()
{
    if (!Input::QuickTransformFired())
        return;
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;

    // The morph holds the game-freeze flag for its whole duration. Mashing ZR+Y so
    // a rising edge re-enters procCoMetamorphoseInit before the previous transform
    // has cleared that flag leaves it stuck on -- so gate on it: never start a
    // transform while a freeze is in effect. (Quick transform only fires with the
    // menu closed, so this won't see our own freeze-on-menu; FlyCam owning the
    // freeze likewise means you're not transforming.)
    if (!dCam_getFreeze())
        dPlayer_quickTransform(link);
}

// Infinite Ammo: keep arrows / bombs / slingshot seeds / lantern oil topped up.
// These are the live counts in the save block's item record (arrows @ info+0xEC,
// bombs[3] @ +0xED.., seeds @ +0xF4 -- all user-confirmed), refilled to each
// item's capacity. Bomb counts are bag-position based, so each bag must map its
// current item id to the matching mItemMax entry. Empty/non-bomb bags are left
// alone. Seeds have no max field -> the slingshot's fixed 50. Lantern oil is in
// the status block (mOil @ info+0x0A) -> refilled to mMaxOil.
static void cheat_infiniteAmmo()
{
    volatile dSv_itemRecord_c* r = g_svItemRecord;
    volatile dSv_itemMax_c*    m = g_svItemMax;
    r->mArrowNum = m->mItemMax[DSV_MAX_ARROW];
    for (int bag = 0; bag < 3; ++bag) {
        const u8 item = g_svItem->mItems[DSV_BOMB_SLOT_FIRST + bag];
        int maxIndex = -1;
        if (item == DSV_ITEM_NORMAL_BOMB)
            maxIndex = DSV_MAX_NORMAL_BOMB;
        else if (item == DSV_ITEM_WATER_BOMB)
            maxIndex = DSV_MAX_WATER_BOMB;
        else if (item == DSV_ITEM_POKE_BOMB)
            maxIndex = DSV_MAX_POKE_BOMB;
        if (maxIndex >= 0)
            r->mBombNum[bag] = m->mItemMax[maxIndex];
    }
    r->mPachinkoNum = DSV_MAX_PACHINKO;
    g_svStatusA->mOil = g_svStatusA->mMaxOil;   // lantern oil
}

// Invincibility: peg Link's post-hit invincibility ("i-frame") timer @ link+0x570
// to a large value every frame, so the damage handler always sees him invincible
// and skips contact/hazard damage. (Doesn't cover instant-death void-outs; pair
// with Infinite Hearts for those.)
#define DPLAYER_OFF_IFRAME 0x570u
static void cheat_invincibility()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    *(volatile u16*)((u8*)link + DPLAYER_OFF_IFRAME) = 2;
}

// tpgz's Unrestricted Items hook returns 1 whenever its cheat is enabled and
// otherwise calls daAlink_c::checkCastleTownUseItem normally. TPHD's equivalent
// is FUN_0201b24c (Ghidra-confirmed KANTERA/COPY_ROD/HVY_BOOTS checks). The
// equipment backend installs a permanent failure-return hook before Cemu JITs
// this function; this top-level cheat changes only the hook's data flag.
static void setUnrestrictedItems(bool on)
{
    EquipmentModifiers::SetUnrestrictedItemsEnabled(on);
}

// Tick marker for the registry. The OFF state is also published in Cheats::Tick
// because disabled registry callbacks are not otherwise invoked.
static void cheat_unrestrictedItems()
{
    setUnrestrictedItems(true);
}

enum BaseCheatId {
    BASE_MOON_JUMP = 0,
    BASE_INFINITE_HEARTS,
    BASE_INFINITE_RUPEES,
    BASE_INFINITE_AIR,
    BASE_INFINITE_AMMO,
    BASE_INVINCIBILITY,
    BASE_QUICK_TRANSFORM,
    BASE_UNRESTRICTED_ITEMS,
};

static Cheat s_cheats[] = {
    // Tooltips for Moon Jump / Quick Transform are built live in the menu
    // (their combos are rebindable via Settings -> Rebind Hotkeys).
    { "Moon Jump",        nullptr,              false, cheat_moonJump },
    { "Infinite Hearts",  nullptr,              false, cheat_infiniteHearts },
    { "Infinite Rupees",  nullptr,              false, cheat_infiniteRupees },
    { "Infinite Air",     nullptr,              false, cheat_infiniteAir },
    { "Infinite Ammo",    "Keep arrows/bombs/seeds/oil topped up",   false, cheat_infiniteAmmo },
    { "Invincibility",    nullptr,              false, cheat_invincibility },
    { "Quick Transform",  nullptr,              false, cheat_quickTransform },
    { "Unrestricted Items", "Use swords and items in normally restricted areas", false, cheat_unrestrictedItems },
};

static const int kBaseCount = (int)(sizeof(s_cheats) / sizeof(s_cheats[0]));

// --- persistence API (see cheats.h): name-addressed enable state for the config.
// A restored-enabled cheat starts ticking in the same frame Config::Load runs.
int Count()
{
    return kBaseCount + EquipmentModifiers::ConfigCount() +
           Difficulty::ConfigCount();
}

const char* Name(int i)
{
    if (i >= 0 && i < kBaseCount)
        return s_cheats[i].name;
    i -= kBaseCount;
    const int equipmentCount = EquipmentModifiers::ConfigCount();
    if (i < equipmentCount)
        return EquipmentModifiers::ConfigName(i);
    return Difficulty::ConfigName(i - equipmentCount);
}

bool IsEnabled(int i)
{
    if (i >= 0 && i < kBaseCount)
        return s_cheats[i].enabled;
    i -= kBaseCount;
    const int equipmentCount = EquipmentModifiers::ConfigCount();
    if (i < equipmentCount)
        return EquipmentModifiers::ConfigEnabled(i);
    return Difficulty::ConfigEnabled(i - equipmentCount);
}

void SetEnabled(int i, bool on)
{
    if (i >= 0 && i < kBaseCount)
        s_cheats[i].enabled = on;
    else {
        i -= kBaseCount;
        const int equipmentCount = EquipmentModifiers::ConfigCount();
        if (i < equipmentCount)
            EquipmentModifiers::SetConfigEnabled(i, on);
        else
            Difficulty::SetConfigEnabled(i - equipmentCount, on);
    }
}

static void drawBaseCheat(BaseCheatId id)
{
    Cheat& cheat = s_cheats[(int)id];
    const bool changed = ImGui::Checkbox(cheat.name, &cheat.enabled);
    if (changed && cheat.tick == cheat_unrestrictedItems)
        setUnrestrictedItems(cheat.enabled);

    if (cheat.tick == cheat_moonJump && ImGui::IsItemHovered()) {
        char hotkey[64];
        Input::HotkeyToString(ov::g_settings.moonJumpCombo,
                              hotkey, sizeof(hotkey));
        ImGui::SetTooltip("Hold %s", hotkey);
    } else if (cheat.tick == cheat_quickTransform && ImGui::IsItemHovered()) {
        char hotkey[64];
        Input::HotkeyToString(ov::g_settings.quickTransformCombo,
                              hotkey, sizeof(hotkey));
        ImGui::SetTooltip("Press %s to switch human/wolf", hotkey);
    } else if (cheat.desc && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", cheat.desc);
    }
}

void DrawGameplayMenu()
{
    if (ImGui::BeginMenu("Player")) {
        drawBaseCheat(BASE_MOON_JUMP);
        drawBaseCheat(BASE_QUICK_TRANSFORM);
        drawBaseCheat(BASE_INVINCIBILITY);
        drawBaseCheat(BASE_INFINITE_HEARTS);
        drawBaseCheat(BASE_INFINITE_AIR);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Items & Equipment")) {
        ImGui::SeparatorText("Editor");
        InventoryEditor::DrawMenuItem();

        ImGui::SeparatorText("Resources");
        drawBaseCheat(BASE_INFINITE_AMMO);
        drawBaseCheat(BASE_INFINITE_RUPEES);

        ImGui::SeparatorText("Item Rules");
        drawBaseCheat(BASE_UNRESTRICTED_ITEMS);

        ImGui::SeparatorText("Modifiers");
        if (ImGui::BeginMenu("Equipment Modifiers")) {
            EquipmentModifiers::DrawMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Difficulty")) {
        Difficulty::DrawMenu();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Movement & World")) {
        QoL::DrawMenu();
        ImGui::EndMenu();
    }
}

void Tick()
{
    // Arm/disarm the input-hook Y takeover to match the Quick Transform checkbox
    // (the hook runs before the game reads input, so it must know every frame --
    // not just while the cheat's tick runs).
    bool qtOn = false;
    for (int i = 0; i < kBaseCount; ++i)
        if (s_cheats[i].tick == cheat_quickTransform)
            qtOn = s_cheats[i].enabled;
    Input::SetQuickTransformArmed(qtOn);

    // Publish Unrestricted Items' OFF state too; registry callbacks below only
    // run while enabled. The permanent hook itself stays installed.
    for (int i = 0; i < kBaseCount; ++i)
        if (s_cheats[i].tick == cheat_unrestrictedItems && !s_cheats[i].enabled)
            setUnrestrictedItems(false);

    for (int i = 0; i < kBaseCount; ++i)
        if (s_cheats[i].enabled && s_cheats[i].tick)
            s_cheats[i].tick();

    EquipmentModifiers::Tick();
    Difficulty::Tick();
    QoL::Tick();
}

void OnApplicationStart()
{
    EquipmentModifiers::OnApplicationStart();
    Difficulty::OnApplicationStart();
    QoL::OnApplicationStart();
    for (int i = 0; i < kBaseCount; ++i)
        if (s_cheats[i].tick == cheat_unrestrictedItems)
            setUnrestrictedItems(s_cheats[i].enabled);
}

void OnApplicationEnd()
{
    setUnrestrictedItems(false);
    QoL::OnApplicationEnd();
    Difficulty::OnApplicationEnd();
    EquipmentModifiers::OnApplicationEnd();
}

} // namespace Cheats
