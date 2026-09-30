#include "cheats/cheat_player.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_horse.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "d/d_item.h"
#include "d/d_save.h"

namespace Cheats {
namespace Player {
struct Entry {
    const char* name;
    const char* controlId;
    const char* configKey;
};

static const Entry kEntries[TOGGLE_COUNT] = {
    { "Moon Jump",                  "gameplay.player.moon_jump",         "cheatMoonJump" },
    { "Quick Transform",            "gameplay.player.quick_transform",   "cheatQuickTransform" },
    { "Invincibility",              "gameplay.player.invincibility",     "cheatInvincibility" },
    { "Infinite Hearts",            "gameplay.player.infinite_hearts",   "cheatInfiniteHearts" },
    { "Infinite Air",               "gameplay.player.infinite_air",      "cheatInfiniteAir" },
    { "Infinite Ammo",              "gameplay.resources.infinite_ammo",  "cheatInfiniteAmmo" },
    { "Infinite Rupees",            "gameplay.resources.infinite_rupees", "cheatInfiniteRupees" },
    { "Infinite Epona Dashes",      "gameplay.movement.epona_dashes",    "cheatEponaDashes" },
    { "Epona Always at Dash Speed", "gameplay.movement.epona_dash_speed", "cheatEponaDashSpeed" },
};

static const float kMoonJumpSpeed = 55.0f;
static const u16   kFullAir = 600;
static const u16   kInvincibleFrames = 2;
static const uint32_t kIFrameOffset = 0x570u;

static bool s_enabled[TOGGLE_COUNT];

static bool valid(Toggle t) { return t >= 0 && t < TOGGLE_COUNT; }

bool IsEnabled(Toggle t)          { return valid(t) && s_enabled[t]; }
const char* Name(Toggle t)        { return valid(t) ? kEntries[t].name : "?"; }
const char* ControlId(Toggle t)   { return valid(t) ? kEntries[t].controlId : ""; }

void SetEnabled(Toggle t, bool enabled)
{
    if (valid(t))
        s_enabled[t] = enabled;
}

static daAlink_c* link()
{
    return (daAlink_c*)dComIfGp_getPlayer();
}

static daHorse_c* mountedHorse()
{
    daAlink_c* l = link();
    return daAlink_isHorseRiding(l) ? dComIfGp_getHorse() : nullptr;
}

static void moonJump()
{
    daAlink_c* l = link();
    if (l && Hotkeys::Held(Hotkeys::HOTKEY_MOON_JUMP))
        l->speed.y = kMoonJumpSpeed;
}

static void quickTransform()
{
    if (!Hotkeys::Pressed(Hotkeys::HOTKEY_QUICK_TRANSFORM))
        return;
    daAlink_c* l = link();
    if (l && !dEvt_isEventRunning())
        daAlink_quickTransform(l);
}

static void invincibility()
{
    daAlink_c* l = link();
    if (l)
        *(volatile u16*)((u8*)l + kIFrameOffset) = kInvincibleFrames;
}

static void infiniteHearts()
{
    volatile dSv_player_status_a_c* status = dComIfGs_getStatusA();
    if (status->mMaxLife)
        status->mLife = (u16)((status->mMaxLife / 5) * 4);
}

static void infiniteAir()
{
    *dComIfGp_getOxygen() = kFullAir;
}

static void infiniteAmmo()
{
    volatile dSv_player_item_record_c* record = dComIfGs_getItemRecord();
    volatile dSv_player_item_max_c* max = dComIfGs_getItemMax();
    record->mArrowNum = max->mItemMax[dSv_MAX_ARROW];
    for (int bag = 0; bag < 3; ++bag) {
        const u8 item = dComIfGs_getItem()->mItems[dSv_BOMB_SLOT_FIRST + bag];
        int maxIndex = -1;
        if (item == dItem_NORMAL_BOMB)
            maxIndex = dSv_MAX_NORMAL_BOMB;
        else if (item == dItem_WATER_BOMB)
            maxIndex = dSv_MAX_WATER_BOMB;
        else if (item == dItem_POKE_BOMB)
            maxIndex = dSv_MAX_POKE_BOMB;
        if (maxIndex >= 0)
            record->mBombNum[bag] = max->mItemMax[maxIndex];
    }
    record->mPachinkoNum = dSv_MAX_PACHINKO;
    volatile dSv_player_status_a_c* status = dComIfGs_getStatusA();
    status->mOil = status->mMaxOil;
}

static void infiniteRupees()
{
    const u16 max = dSv_getRupeeMax((const dSv_player_status_a_c*)dComIfGs_getStatusA());
    if (max)
        dComIfGs_getStatusA()->mRupee = max;
}

bool WantsInputBlock(uint32_t held)
{
    if (!s_enabled[QUICK_TRANSFORM] || !Hotkeys::Matches(Hotkeys::HOTKEY_QUICK_TRANSFORM, held))
        return false;
    daAlink_c* l = link();
    return l && daAlink_canTransform(l);
}

void Tick(bool acceptInput)
{
    if (!dComIfGp_getPlayer())
        return;
    if (acceptInput && s_enabled[MOON_JUMP])
        moonJump();
    if (acceptInput && s_enabled[QUICK_TRANSFORM])
        quickTransform();
    if (s_enabled[INVINCIBILITY])
        invincibility();
    if (s_enabled[INFINITE_HEARTS])
        infiniteHearts();
    if (s_enabled[INFINITE_AIR])
        infiniteAir();
    if (s_enabled[INFINITE_AMMO])
        infiniteAmmo();
    if (s_enabled[INFINITE_RUPEES])
        infiniteRupees();
    if (s_enabled[EPONA_DASHES])
        daHorse_refillDashes(mountedHorse());
    if (s_enabled[EPONA_DASH_SPEED])
        daHorse_holdDashSpeed(mountedHorse());
}

void ResetToDefaults()
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        s_enabled[i] = false;
}

void LoadConfig(const cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::ReadBool(root, kEntries[i].configKey, &s_enabled[i]);
}

void SaveConfig(cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::AddBool(root, kEntries[i].configKey, s_enabled[i]);
}
}
}
