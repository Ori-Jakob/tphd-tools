// cheats/cheats.cpp -- see cheats.h.
//
// Registry of tpgz-style cheats. To add one:
//   1. Write a `static void cheat_xxx() { ... }` that pokes game state (use the
//      game/ bindings; add new structs/addresses to include/game/ as needed).
//   2. Add a row to s_cheats below: { "Name", "Tooltip", false, cheat_xxx }.
// `tick` runs every frame while the cheat's checkbox is on. Leave `tick` null
// for cheats that only need a one-shot button (draw that in DrawMenu directly).
#include "cheats/cheats.h"
#include "cheats/inventory_editor.h"
#include "input.h"

#include <coreinit/cache.h>   // DCFlushRange / ICInvalidateRange (code-patch cheats)

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

// Moon Jump: hold ZR+A to drive Link straight up. n0ted's MegaCheats writes the
// gravity value into Link's Y velocity (actor+0x4FC = speed.y) while the combo
// is held; we do the same from the present hook.
static void cheat_moonJump()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    GameInput in;
    if (!dCam_getInput(&in))
        return;
    u32 b = dCam_normalizeButtons(in.buttons, in.type);
    const u32 combo = PRO_BTN_ZR | PRO_BTN_A;   // 0x14, MegaCheats' default
    if ((b & combo) == combo)
        link->speed.y = 55.0f;
}

// Infinite Hearts: keep the live current life full ((maxLife/5)*4). Writes the
// HUD/gameplay meter copy (g_meterInfo), not the persistent save block.
static void cheat_infiniteHearts()
{
    if (g_meterInfo->mMaxLife)
        g_meterInfo->mLife = (u16)((g_meterInfo->mMaxLife / 5) * 4);
}

// Infinite Rupees: keep the live rupee count topped up.
static void cheat_infiniteRupees()
{
    g_meterInfo->mRupee = 999;
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
// item's capacity (mItemMax, which is 0 when you don't own the item, so nothing
// phantom is added). Seeds have no max field -> the slingshot's fixed 50. Lantern
// oil is in the status block (mOil @ info+0x0A) -> refilled to mMaxOil.
static void cheat_infiniteAmmo()
{
    volatile dSv_itemRecord_c* r = g_svItemRecord;
    volatile dSv_itemMax_c*    m = g_svItemMax;
    r->mArrowNum    = m->mItemMax[DSV_MAX_ARROW];
    r->mBombNum[0]  = m->mItemMax[DSV_MAX_NORMAL_BOMB];
    r->mBombNum[1]  = m->mItemMax[DSV_MAX_WATER_BOMB];
    r->mBombNum[2]  = m->mItemMax[DSV_MAX_POKE_BOMB];
    r->mPachinkoNum = 50;
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

// Unrestricted Items: tpgz hooks `daAlink_c::checkCastleTownUseItem(u16)` so it
// always returns 1 ("item is usable here") -- letting you use any item in areas
// that normally restrict them (Castle Town, boss rooms, etc.). That's a function
// RETURN override, not a memory value, so unlike the other cheats we don't poke a
// field each frame: we patch the function's entry to `li r3,1; blr` while the
// cheat is on and restore the original two instructions when off.
// Function = FUN_0201b24c (Ghidra-confirmed: KANTERA 0x48 / COPY_ROD 0x46 +
// "R_SP128" / HVY_BOOTS 0x45, returns 1=usable / 0=restricted). The patch is
// applied/removed on the checkbox edge (see Cheats::Tick), not per frame.
#define ADDR_checkCastleTownUseItem 0x0201b24cu

static bool s_unrestrictedPatched = false;
static u32  s_unrestrictedOrig[2] = { 0, 0 };

static void setUnrestrictedItems(bool on)
{
    if (on == s_unrestrictedPatched)
        return;   // idempotent: only write on a real on<->off transition
    volatile u32* fn = (volatile u32*)ADDR_checkCastleTownUseItem;
    if (on) {
        s_unrestrictedOrig[0] = fn[0];   // stash the real prologue for restore
        s_unrestrictedOrig[1] = fn[1];
        fn[0] = 0x38600001;   // li   r3, 1
        fn[1] = 0x4E800020;   // blr
    } else {
        fn[0] = s_unrestrictedOrig[0];
        fn[1] = s_unrestrictedOrig[1];
    }
    // Make the CPU (and Cemu's recompiler) see the rewritten instructions.
    DCFlushRange((void*)fn, 8);
    ICInvalidateRange((void*)fn, 8);
    s_unrestrictedPatched = on;
}

// Tick marker for the registry: applies the patch while the checkbox is on. The
// OFF transition (restore) is handled in Cheats::Tick, since a cheat's tick only
// runs while it's enabled and a code patch must be actively removed.
static void cheat_unrestrictedItems()
{
    setUnrestrictedItems(true);
}

static Cheat s_cheats[] = {
    { "Moon Jump",        "Hold ZR+A",          false, cheat_moonJump },
    { "Infinite Hearts",  nullptr,              false, cheat_infiniteHearts },
    { "Infinite Rupees",  nullptr,              false, cheat_infiniteRupees },
    { "Infinite Air",     nullptr,              false, cheat_infiniteAir },
    { "Infinite Ammo",    "Keep arrows/bombs/seeds/oil topped up",   false, cheat_infiniteAmmo },
    { "Invincibility",    nullptr,              false, cheat_invincibility },
    { "Quick Transform",  "Press ZR+Y to switch human/wolf",     false, cheat_quickTransform }
    //{ "Unrestricted Items", "Use any item anywhere (no area limits)", false, cheat_unrestrictedItems },
};

static const int kCount = (int)(sizeof(s_cheats) / sizeof(s_cheats[0]));

// --- persistence API (see cheats.h): name-addressed enable state for the config.
// "Unrestricted Items" patches game code on its OFF edge via Cheats::Tick, so it
// is safe to restore here too -- a restored-enabled cheat just starts ticking.
int         Count()                  { return kCount; }
const char* Name(int i)              { return (i >= 0 && i < kCount) ? s_cheats[i].name : ""; }
bool        IsEnabled(int i)         { return i >= 0 && i < kCount && s_cheats[i].enabled; }
void        SetEnabled(int i, bool on) { if (i >= 0 && i < kCount) s_cheats[i].enabled = on; }

void DrawMenu()
{
    for (int i = 0; i < kCount; ++i) {
        ImGui::Checkbox(s_cheats[i].name, &s_cheats[i].enabled);
        if (s_cheats[i].desc && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", s_cheats[i].desc);
    }
    ImGui::Separator();
    InventoryEditor::DrawMenuButton();
}

void Tick()
{
    // Arm/disarm the input-hook Y takeover to match the Quick Transform checkbox
    // (the hook runs before the game reads input, so it must know every frame --
    // not just while the cheat's tick runs).
    bool qtOn = false;
    for (int i = 0; i < kCount; ++i)
        if (s_cheats[i].tick == cheat_quickTransform)
            qtOn = s_cheats[i].enabled;
    Input::SetQuickTransformArmed(qtOn);

    // Unrestricted Items patches game code, so the OFF state must be applied
    // actively -- the per-frame tick below only runs while a cheat is enabled.
    // The enabled tick re-applies (idempotent); here we remove it when cleared.
    for (int i = 0; i < kCount; ++i)
        if (s_cheats[i].tick == cheat_unrestrictedItems && !s_cheats[i].enabled)
            setUnrestrictedItems(false);

    for (int i = 0; i < kCount; ++i)
        if (s_cheats[i].enabled && s_cheats[i].tick)
            s_cheats[i].tick();
}

} // namespace Cheats
