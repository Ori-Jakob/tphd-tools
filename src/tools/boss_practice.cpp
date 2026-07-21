// boss_practice.cpp -- see boss_practice.h.
//
// Encounter launches are built in a temporary dSv_info_c image. Only after the
// user presses Start does SaveState copy that image and inject it at phase_1.
// The outgoing scene and live save are never used as a staging buffer.
#include "tools/boss_practice.h"

#include "imgui.h"
#include "logger.h"
#include "tools/save_state.h"
#include "game/c_damagereaction.h"
#include "game/d_actor.h"
#include "game/d_ami_shutter.h"
#include "game/d_camera.h"
#include "game/d_com_inf_game.h"
#include "game/d_event.h"
#include "game/d_inventory.h"
#include "game/d_save.h"
#include "game/d_stage.h"

#include <algorithm>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Tools {
namespace BossPractice {

enum LoadoutSource {
    LOADOUT_CURRENT = 0,
    LOADOUT_RECOMMENDED,
    LOADOUT_CUSTOM,
};

struct BossLoadout {
    int maxHearts;
    int currentQuarters;
    int rupees;
    int oil;
    int maxOil;
    u8 armor;
    u8 sword;
    u8 shield;
    u8 form;

    bool boomerang;
    bool lantern;
    bool spinner;
    bool ironBoots;
    bool bow;
    bool hawkeye;
    bool ballAndChain;
    bool dominionRod;
    bool clawshot;
    bool doubleClawshots;
    bool slingshot;

    int arrows;
    int maxArrows;
    int seeds;
    u8 bombBag[3];
    int bombCount[3];
    u8 bottle[4];
    u8 bottleAmount[4];
    int maxBombs;
    int maxWaterBombs;
    int maxBomblings;
};

struct EncounterEntry {
    s16 spawn;
    SaveState::PracticeSceneSetupFn sceneSetup;
    bool useCapturedView;
    cXyz position;
    s16 angle;
    CameraXform camera;
    bool triggerOpeningLook;
};

struct BossDefinition;
typedef BossLoadout (*RecommendedLoadoutFn)();
typedef void (*PrepareEncounterFn)(u8* info, const BossDefinition& boss,
                                   const EncounterEntry& entry);

struct BossDefinition {
    const char* category;
    const char* name;
    const char* stage;
    s8 room;
    s8 layer;
    s8 stageNo;
    s16 expectedProc;
    EncounterEntry opening;
    EncounterEntry fight;
    RecommendedLoadoutFn recommendedLoadout;
    PrepareEncounterFn prepareEncounter;
};

static BossLoadout recommendedDekuToad();
static void prepareDekuToadImage(u8* info, const BossDefinition& boss,
                                 const EncounterEntry& entry);

static void setupDekuToadOpening()
{
    // Explicitly clear a stale skip handoff. E_DT create then follows its native
    // ACT_OPENING branch and owns the camera/event sequence.
    cDmr_setSkipInfo(0);
}

static void setupDekuToadFight()
{
    // This is exactly the handoff written by E_DT::demo_skip in TPHD. The room
    // is entered at point 2, and E_DT create consumes+clears it before choosing
    // ACT_WAIT, positioning itself, starting BGM, and setting one-zone switch 3.
    cDmr_setSkipInfo(1);
}

static const BossDefinition kBosses[] = {
    {
        "Lakebed Temple", "Deku Toad", "D_MN01B", 51,
        DSTAGE_LAYER_DEFAULT, 18, FPCNM_E_DT,
        {
            3, setupDekuToadOpening, true,
            // Captured from LBT_Mini_Boss_Start.bin on the exact frame E_DT
            // entered its subject-camera sight branch (mode 3).
            { -4.825231f, 0.0f, 1463.19531f }, (s16)-32768,
            {
                { -4.825231f, 130.134476f, 1462.19531f },
                { -4.825231f, 208.321716f, 1754.18066f },
            },
            true,
        },
        {
            2, setupDekuToadFight, false,
            { 0.0f, 0.0f, 0.0f }, 0,
            { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
            false,
        },
        recommendedDekuToad,
        prepareDekuToadImage,
    },
};

static const int kBossCount = (int)(sizeof(kBosses) / sizeof(kBosses[0]));

static bool s_enabled = false;
static int s_selectedBoss = 0;
static bool s_playOpening = true;
static int s_loadoutSource = LOADOUT_CURRENT;
static BossLoadout s_custom = {};
static bool s_customReady = false;
static char s_status[160] = "";
static bool s_validateLaunch = false;
static int s_validateFrames = 0;
static int s_validateTotalFrames = 0;
static int s_validateBoss = -1;
static bool s_triggerOpeningLook = false;
static bool s_openingStatusCaptured = false;
static bool s_openingStatusWasSet = false;
static bool s_openingShutterClosed = false;

// TPHD daE_DT_c fields confirmed in FUN_02232f5c/FUN_0223069c. ACT_OPENING is
// action 10; its native sight branch advances mMode from 1/2 to 3 before it
// orders the potential event and starts the real pre-fight cinematic.
#define DEKU_TOAD_OFF_ACTION 0x070Cu
#define DEKU_TOAD_OFF_MODE   0x0710u
#define DEKU_TOAD_ACT_OPENING 10

static void releaseOpeningLookStatus()
{
    if (s_openingStatusCaptured && !s_openingStatusWasSet)
        *(volatile u32*)GAME_ADDR_playerStatus0 &= ~DPLY_STATUS0_ZTARGET;
    s_openingStatusCaptured = false;
    s_openingStatusWasSet = false;
}

static int clampInt(int value, int lo, int hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static const dSv_status_a_c* statusFrom(const u8* info)
{
    return (const dSv_status_a_c*)(info + DSV_INFO_STATUS_A_OFF);
}

static dSv_status_a_c* statusFrom(u8* info)
{
    return (dSv_status_a_c*)(info + DSV_INFO_STATUS_A_OFF);
}

static const dSv_item_c* itemsFrom(const u8* info)
{
    return (const dSv_item_c*)(info + DSV_INFO_ITEM_OFF);
}

static dSv_item_c* itemsFrom(u8* info)
{
    return (dSv_item_c*)(info + DSV_INFO_ITEM_OFF);
}

static const dSv_itemRecord_c* recordFrom(const u8* info)
{
    return (const dSv_itemRecord_c*)(info + DSV_INFO_ITEM_RECORD_OFF);
}

static dSv_itemRecord_c* recordFrom(u8* info)
{
    return (dSv_itemRecord_c*)(info + DSV_INFO_ITEM_RECORD_OFF);
}

static const dSv_itemMax_c* maxFrom(const u8* info)
{
    return (const dSv_itemMax_c*)(info + DSV_INFO_ITEM_MAX_OFF);
}

static dSv_itemMax_c* maxFrom(u8* info)
{
    return (dSv_itemMax_c*)(info + DSV_INFO_ITEM_MAX_OFF);
}

static void captureLoadout(BossLoadout& out, const u8* info)
{
    const dSv_status_a_c* status = statusFrom(info);
    const dSv_item_c* items = itemsFrom(info);
    const dSv_itemRecord_c* record = recordFrom(info);
    const dSv_itemMax_c* itemMax = maxFrom(info);

    memset(&out, 0, sizeof(out));
    out.maxHearts = clampInt(status->mMaxLife / 5, 1, 20);
    out.currentQuarters = clampInt(status->mLife, 1, out.maxHearts * 4);
    out.rupees = status->mRupee;
    out.oil = status->mOil;
    out.maxOil = status->mMaxOil;
    out.armor = status->mSelectEquip[DSV_EQUIP_ARMOR];
    out.sword = status->mSelectEquip[DSV_EQUIP_SWORD];
    out.shield = status->mSelectEquip[DSV_EQUIP_SHIELD];
    out.form = status->mTransformStatus ? 1 : 0;

    out.boomerang = items->mItems[0] == 0x40;
    out.lantern = items->mItems[1] == 0x48 || items->mItems[1] == 0xF8;
    out.spinner = items->mItems[2] == 0x41;
    out.ironBoots = items->mItems[3] == 0x45;
    out.bow = items->mItems[4] == 0x43;
    out.hawkeye = items->mItems[5] == 0x3E;
    out.ballAndChain = items->mItems[6] == 0x42;
    out.dominionRod = items->mItems[8] == 0x4C || items->mItems[8] == 0x46;
    out.clawshot = items->mItems[9] == 0x44;
    out.doubleClawshots = items->mItems[10] == 0x47;
    out.slingshot = items->mItems[23] == 0x4B;

    out.arrows = record->mArrowNum;
    out.maxArrows = itemMax->mItemMax[DSV_MAX_ARROW];
    out.seeds = record->mPachinkoNum;
    for (int i = 0; i < 3; ++i) {
        out.bombBag[i] = items->mItems[15 + i];
        out.bombCount[i] = record->mBombNum[i];
    }
    for (int i = 0; i < 4; ++i) {
        out.bottle[i] = items->mItems[11 + i];
        out.bottleAmount[i] = record->mBottleNum[i];
    }
    out.maxBombs = itemMax->mItemMax[DSV_MAX_NORMAL_BOMB];
    out.maxWaterBombs = itemMax->mItemMax[DSV_MAX_WATER_BOMB];
    out.maxBomblings = itemMax->mItemMax[DSV_MAX_POKE_BOMB];
}

static BossLoadout recommendedDekuToad()
{
    BossLoadout out = {};
    out.maxHearts = 6;
    out.currentQuarters = 24;
    out.rupees = 100;
    out.maxOil = 600;
    out.oil = 600;
    out.armor = 47;               // Hero's Clothes
    out.sword = 40;               // Ordon Sword
    out.shield = 44;              // Hylian Shield
    out.form = 0;
    out.boomerang = true;
    out.lantern = true;
    out.ironBoots = true;
    out.bow = true;
    out.slingshot = true;
    out.arrows = 30;
    out.maxArrows = 30;
    out.seeds = 50;
    out.bombBag[0] = 0x70;        // Normal Bombs
    out.bombCount[0] = 30;
    out.bombBag[1] = DSV_ITEM_NONE;
    out.bombBag[2] = DSV_ITEM_NONE;
    out.maxBombs = 30;
    out.maxWaterBombs = 15;
    out.maxBomblings = 10;
    out.bottle[0] = 0x6C;         // Fairy
    out.bottle[1] = 0x60;         // Empty Bottle
    out.bottle[2] = DSV_ITEM_NONE;
    out.bottle[3] = DSV_ITEM_NONE;
    return out;
}

static void setInfoBit(u8* info, u32 off, int bit, bool on)
{
    u8* byte = info + off;
    const u8 mask = (u8)(1u << bit);
    if (on)
        *byte |= mask;
    else
        *byte &= (u8)~mask;
}

static void setGotItem(u8* info, u8 item)
{
    if (item == DSV_ITEM_NONE)
        return;
    dSv_getItem_c* got = (dSv_getItem_c*)(info + DSV_INFO_GET_ITEM_OFF);
    got->mItemFlags[item >> 5] |= 1u << (item & 31);
}

static void setSlot(u8* info, int slot, bool have, u8 item)
{
    dSv_item_c* items = itemsFrom(info);
    items->mItems[slot] = have ? item : (u8)DSV_ITEM_NONE;
    if (have)
        setGotItem(info, item);
}

static void ensureOwnedEquipment(u8* info, const BossLoadout& loadout)
{
    if (loadout.armor == 47)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 7, true);
    else if (loadout.armor == 48)
        setInfoBit(info, DSV_OFF_HAVE_ARMOR, 0, true);
    else if (loadout.armor == 49)
        setInfoBit(info, DSV_OFF_HAVE_ARMOR, 1, true);

    if (loadout.sword == 40)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 0, true);
    else if (loadout.sword == 41)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 1, true);
    else if (loadout.sword == 73) {
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 1, true);
        setInfoBit(info, DSV_OFF_HAVE_MSLIGHT, 1, true);
    }

    if (loadout.shield == 42)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 2, true);
    else if (loadout.shield == 43)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 3, true);
    else if (loadout.shield == 44)
        setInfoBit(info, DSV_OFF_HAVE_SWDSHLD, 4, true);
}

static void applyLoadout(u8* info, const BossLoadout& requested)
{
    BossLoadout loadout = requested;
    loadout.maxHearts = clampInt(loadout.maxHearts, 1, 20);
    loadout.currentQuarters =
        clampInt(loadout.currentQuarters, 1, loadout.maxHearts * 4);

    dSv_status_a_c* status = statusFrom(info);
    status->mMaxLife = (u16)(loadout.maxHearts * 5);
    status->mLife = (u16)loadout.currentQuarters;
    status->mRupee = (u16)clampInt(loadout.rupees, 0, 9999);
    status->mOil = (u16)clampInt(loadout.oil, 0, 65535);
    status->mMaxOil = (u16)clampInt(loadout.maxOil, 0, 65535);
    status->mSelectEquip[DSV_EQUIP_ARMOR] = loadout.armor;
    status->mSelectEquip[DSV_EQUIP_SWORD] = loadout.sword;
    status->mSelectEquip[DSV_EQUIP_SHIELD] = loadout.shield;
    status->mTransformStatus = loadout.form ? 1 : 0;
    ensureOwnedEquipment(info, loadout);

    setSlot(info, 0, loadout.boomerang, 0x40);
    setSlot(info, 1, loadout.lantern, 0x48);
    setSlot(info, 2, loadout.spinner, 0x41);
    setSlot(info, 3, loadout.ironBoots, 0x45);
    setSlot(info, 4, loadout.bow, 0x43);
    setSlot(info, 5, loadout.hawkeye, 0x3E);
    setSlot(info, 6, loadout.ballAndChain, 0x42);
    setSlot(info, 8, loadout.dominionRod, 0x4C);
    setSlot(info, 9, loadout.clawshot, 0x44);
    setSlot(info, 10, loadout.doubleClawshots, 0x47);
    setSlot(info, 23, loadout.slingshot, 0x4B);

    dSv_item_c* items = itemsFrom(info);
    dSv_itemRecord_c* record = recordFrom(info);
    dSv_itemMax_c* itemMax = maxFrom(info);
    record->mArrowNum = (u8)clampInt(loadout.arrows, 0, 100);
    record->mPachinkoNum = (u8)clampInt(loadout.seeds, 0, 100);
    itemMax->mItemMax[DSV_MAX_ARROW] =
        (u8)clampInt(std::max(loadout.maxArrows, loadout.arrows), 0, 100);
    for (int i = 0; i < 3; ++i) {
        items->mItems[15 + i] = loadout.bombBag[i];
        record->mBombNum[i] = (u8)clampInt(loadout.bombCount[i], 0, 99);
        setGotItem(info, loadout.bombBag[i]);
    }
    for (int i = 0; i < 4; ++i) {
        items->mItems[11 + i] = loadout.bottle[i];
        record->mBottleNum[i] = loadout.bottleAmount[i];
        setGotItem(info, loadout.bottle[i]);
    }
    int normalMax = loadout.maxBombs;
    int waterMax = loadout.maxWaterBombs;
    int bomblingMax = loadout.maxBomblings;
    for (int i = 0; i < 3; ++i) {
        if (loadout.bombBag[i] == 0x70)
            normalMax = std::max(normalMax, loadout.bombCount[i]);
        else if (loadout.bombBag[i] == 0x71)
            waterMax = std::max(waterMax, loadout.bombCount[i]);
        else if (loadout.bombBag[i] == 0x72)
            bomblingMax = std::max(bomblingMax, loadout.bombCount[i]);
    }
    itemMax->mItemMax[DSV_MAX_NORMAL_BOMB] =
        (u8)clampInt(normalMax, 0, 99);
    itemMax->mItemMax[DSV_MAX_WATER_BOMB] =
        (u8)clampInt(waterMax, 0, 99);
    itemMax->mItemMax[DSV_MAX_POKE_BOMB] =
        (u8)clampInt(bomblingMax, 0, 99);

    // A captured button assignment may point at a slot the custom loadout just
    // emptied. Clear only those invalid assignments; the engine rebuilds lineup
    // ordering during dSave_loadImage at the practice scene barrier.
    for (int i = 0; i < 4; ++i) {
        const u8 selected = status->mSelectItem[i];
        if (selected >= 24 || items->mItems[selected] == DSV_ITEM_NONE) {
            status->mSelectItem[i] = DSV_ITEM_NONE;
            status->mMixItem[i] = DSV_ITEM_NONE;
        }
    }
}

static dSv_memBit_c* savedStageBit(u8* info, int stageNo)
{
    return (dSv_memBit_c*)(info + DSV_INFO_STAGE_RECORDS_OFF +
                          (u32)stageNo * DSV_STAGE_RECORD_SIZE);
}

static void initializeDanForStage(u8* info, s8 stageNo)
{
    u8* dan = info + DSV_INFO_DAN_OFF;
    memset(dan, 0, DSV_INFO_DAN_SIZE);
    dan[0] = (u8)stageNo;
    for (int i = 0; i < 16; ++i)
        *(u16*)(dan + 0x1C + i * sizeof(u16)) = 0xFFFFu;
}

static void clearTransientSceneState(u8* info)
{
    memset(info + DSV_INFO_ZONE_OFF, 0, DSV_INFO_ZONE_SIZE);
    for (int i = 0; i < DSV_ZONE_RECORD_COUNT; ++i)
        info[DSV_INFO_ZONE_OFF + i * DSV_ZONE_RECORD_SIZE] = 0xFF;
    memset(info + DSV_INFO_TMP_EVENT_OFF, 0, DSV_INFO_TMP_EVENT_SIZE);
}

static void prepareDekuToadImage(u8* info, const BossDefinition& boss,
                                 const EncounterEntry& entry)
{
    // E_DT's two native create gates in TPHD:
    //   FUN_02aa87ec(..., actor param low byte 4, room 51) -> stage switch 4
    //   FUN_02aa7d94(mMemory, 7)                         -> miniboss defeated
    // Clear both in the target area's persistent record, then make its live
    // mMemory match. This does not disturb unrelated Lakebed progression.
    dSv_memBit_c* stage = savedStageBit(info, boss.stageNo);
    dMem_setBit(stage->mSwitch, 4, false);
    stage->mDungeonItem &= (u8)~(1u << DMEM_MINIBOSS);
    memcpy(info + DSV_INFO_MEMORY_OFF, stage, DSV_INFO_MEMORY_SIZE);

    initializeDanForStage(info, boss.stageNo);
    clearTransientSceneState(info);

    dSv_restart_c* restart = dSv_getRestartFromInfo(info);
    memset(restart, 0, sizeof(*restart));
    restart->mRoomNo = boss.room;
    restart->mStartPoint = entry.spawn;
    restart->mRoomParam = 0xFF000000u | ((u32)boss.room & 0x3Fu);
}

struct Choice { u8 id; const char* name; };

static void drawChoice(const char* label, u8& value, const Choice* choices, int count)
{
    const char* preview = "Unknown";
    for (int i = 0; i < count; ++i) {
        if (choices[i].id == value) {
            preview = choices[i].name;
            break;
        }
    }
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(choices[i].name, choices[i].id == value))
                value = choices[i].id;
        }
        ImGui::EndCombo();
    }
}

static void drawStatusLoadout(BossLoadout& loadout)
{
    ImGui::SliderInt("Maximum hearts", &loadout.maxHearts, 1, 20);
    const int maxQuarters = std::max(1, loadout.maxHearts * 4);
    loadout.currentQuarters = clampInt(loadout.currentQuarters, 1, maxQuarters);
    ImGui::SliderInt("Current health", &loadout.currentQuarters, 1, maxQuarters,
                     "%d quarter-hearts");
    ImGui::TextDisabled("Current: %d + %d/4 hearts",
                        loadout.currentQuarters / 4,
                        loadout.currentQuarters % 4);
    ImGui::InputInt("Rupees", &loadout.rupees);
    ImGui::InputInt("Lantern oil", &loadout.oil);
    ImGui::InputInt("Maximum oil", &loadout.maxOil);
    bool wolf = loadout.form != 0;
    if (ImGui::Checkbox("Wolf form", &wolf))
        loadout.form = wolf ? 1 : 0;
}

static void drawEquipmentLoadout(BossLoadout& loadout)
{
    static const Choice armor[] = {
        { 46, "Ordon Clothes" }, { 47, "Hero's Clothes" },
        { 48, "Magic Armor" }, { 49, "Zora Armor" },
    };
    static const Choice sword[] = {
        { 255, "None" }, { 40, "Ordon Sword" }, { 41, "Master Sword" },
        { 73, "Master Sword (Light)" }, { 63, "Wooden Sword" },
    };
    static const Choice shield[] = {
        { 255, "None" }, { 42, "Ordon Shield" }, { 43, "Wooden Shield" },
        { 44, "Hylian Shield" },
    };
    drawChoice("Clothes", loadout.armor, armor, IM_ARRAYSIZE(armor));
    drawChoice("Sword", loadout.sword, sword, IM_ARRAYSIZE(sword));
    drawChoice("Shield", loadout.shield, shield, IM_ARRAYSIZE(shield));

    ImGui::SeparatorText("Combat items");
    if (ImGui::BeginTable("##boss_items", 2, ImGuiTableFlags_SizingStretchSame)) {
        auto item = [](const char* label, bool& value) {
            ImGui::TableNextColumn();
            ImGui::Checkbox(label, &value);
        };
        item("Gale Boomerang", loadout.boomerang);
        item("Lantern", loadout.lantern);
        item("Spinner", loadout.spinner);
        item("Iron Boots", loadout.ironBoots);
        item("Hero's Bow", loadout.bow);
        item("Hawkeye", loadout.hawkeye);
        item("Ball and Chain", loadout.ballAndChain);
        item("Dominion Rod", loadout.dominionRod);
        item("Clawshot", loadout.clawshot);
        item("Double Clawshots", loadout.doubleClawshots);
        item("Slingshot", loadout.slingshot);
        ImGui::EndTable();
    }
}

static void drawConsumablesLoadout(BossLoadout& loadout)
{
    ImGui::InputInt("Arrows", &loadout.arrows);
    ImGui::InputInt("Quiver capacity", &loadout.maxArrows);
    ImGui::InputInt("Slingshot seeds", &loadout.seeds);

    static const Choice bombs[] = {
        { DSV_ITEM_NONE, "None" }, { 0x70, "Bombs" },
        { 0x71, "Water Bombs" }, { 0x72, "Bomblings" },
    };
    ImGui::SeparatorText("Bomb bags");
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        char label[24];
        snprintf(label, sizeof(label), "Bomb Bag %d", i + 1);
        drawChoice(label, loadout.bombBag[i], bombs, IM_ARRAYSIZE(bombs));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("Count", &loadout.bombCount[i]);
        ImGui::PopID();
    }
    if (ImGui::TreeNode("Bomb capacities")) {
        ImGui::InputInt("Bomb capacity", &loadout.maxBombs);
        ImGui::InputInt("Water Bomb capacity", &loadout.maxWaterBombs);
        ImGui::InputInt("Bombling capacity", &loadout.maxBomblings);
        ImGui::TreePop();
    }

    static const Choice bottles[] = {
        { DSV_ITEM_NONE, "None" }, { 0x60, "Empty Bottle" },
        { 0x61, "Red Potion" }, { 0x62, "Green Potion" },
        { 0x63, "Blue Potion" }, { 0x66, "Lantern Oil" },
        { 0x6C, "Fairy" }, { 0x73, "Great Fairy's Tears" },
    };
    ImGui::SeparatorText("Bottles");
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(10 + i);
        char label[24];
        snprintf(label, sizeof(label), "Bottle %d", i + 1);
        drawChoice(label, loadout.bottle[i], bottles, IM_ARRAYSIZE(bottles));
        ImGui::PopID();
    }
}

static void drawLoadoutEditor(BossLoadout& loadout)
{
    if (ImGui::BeginTabBar("##boss_loadout_tabs")) {
        if (ImGui::BeginTabItem("Status")) {
            drawStatusLoadout(loadout);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Equipment")) {
            drawEquipmentLoadout(loadout);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Consumables")) {
            drawConsumablesLoadout(loadout);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void startSelectedEncounter()
{
    if (s_selectedBoss < 0 || s_selectedBoss >= kBossCount)
        return;
    const BossDefinition& boss = kBosses[s_selectedBoss];
    const EncounterEntry& entry = s_playOpening ? boss.opening : boss.fight;

    releaseOpeningLookStatus();
    s_triggerOpeningLook = entry.triggerOpeningLook;
    s_openingShutterClosed = false;

    u8* image = (u8*)memalign(0x40, GAME_DSVINFO_SIZE);
    if (!image) {
        snprintf(s_status, sizeof(s_status), "Unable to allocate the practice image.");
        return;
    }
    memcpy(image, (const void*)GAME_ADDR_gameInfo_info, GAME_DSVINFO_SIZE);
    boss.prepareEncounter(image, boss, entry);

    if (s_loadoutSource == LOADOUT_RECOMMENDED) {
        const BossLoadout recommended = boss.recommendedLoadout();
        applyLoadout(image, recommended);
    } else if (s_loadoutSource == LOADOUT_CUSTOM) {
        if (!s_customReady) {
            captureLoadout(s_custom, image);
            s_customReady = true;
        }
        applyLoadout(image, s_custom);
    }

    const cXyz* position = entry.useCapturedView ? &entry.position : nullptr;
    const cXyz* camAt = entry.useCapturedView ? &entry.camera.at : nullptr;
    const cXyz* camEye = entry.useCapturedView ? &entry.camera.eye : nullptr;
    const bool queued = SaveState::BeginPracticeLoad(
        boss.name, image, GAME_DSVINFO_SIZE, boss.stage, boss.room, entry.spawn,
        boss.layer, position, entry.angle, camAt, camEye, entry.sceneSetup);
    free(image);
    if (!queued) {
        snprintf(s_status, sizeof(s_status),
                 "Unable to start: another scene load is already in progress.");
        return;
    }

    snprintf(s_status, sizeof(s_status), "Loading %s (%s) ...", boss.name,
             s_playOpening ? "Toad cinematic" : "fight start");
    s_validateLaunch = true;
    s_validateFrames = 0;
    s_validateTotalFrames = 0;
    s_validateBoss = s_selectedBoss;
}

void DrawMenuItem()
{
    ImGui::Checkbox("Boss Practice", &s_enabled);
}

bool IsEnabled() { return s_enabled; }
void SetEnabled(bool enabled) { s_enabled = enabled; }

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 590.0f), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("Boss Practice", &s_enabled, flags)) {
        const BossDefinition& boss = kBosses[s_selectedBoss];
        if (ImGui::BeginCombo("Encounter", boss.name)) {
            for (int i = 0; i < kBossCount; ++i) {
                char label[96];
                snprintf(label, sizeof(label), "%s - %s",
                         kBosses[i].category, kBosses[i].name);
                if (ImGui::Selectable(label, i == s_selectedBoss))
                    s_selectedBoss = i;
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s  |  %s room %d", boss.category, boss.stage,
                            (int)boss.room);
        ImGui::Checkbox("Play opening cinematic", &s_playOpening);
        ImGui::TextDisabled(
            s_playOpening
                ? "Starts at the native look-at-Toad cinematic, after the grate closes."
                : "Uses the encounter's verified native demo-skip entry.");

        ImGui::SeparatorText("Loadout");
        ImGui::RadioButton("Current inventory", &s_loadoutSource, LOADOUT_CURRENT);
        ImGui::SameLine();
        ImGui::RadioButton("Recommended", &s_loadoutSource, LOADOUT_RECOMMENDED);
        ImGui::SameLine();
        ImGui::RadioButton("Custom", &s_loadoutSource, LOADOUT_CUSTOM);

        if (s_loadoutSource == LOADOUT_CURRENT) {
            ImGui::TextDisabled("The launch image keeps your current status and inventory.");
        } else if (s_loadoutSource == LOADOUT_RECOMMENDED) {
            BossLoadout recommended = boss.recommendedLoadout();
            ImGui::BeginDisabled();
            drawLoadoutEditor(recommended);
            ImGui::EndDisabled();
        } else {
            if (!s_customReady) {
                captureLoadout(s_custom,
                               (const u8*)GAME_ADDR_gameInfo_info);
                s_customReady = true;
            }
            if (ImGui::Button("Capture Current"))
                captureLoadout(s_custom, (const u8*)GAME_ADDR_gameInfo_info);
            ImGui::SameLine();
            if (ImGui::Button("Use Recommended"))
                s_custom = boss.recommendedLoadout();
            drawLoadoutEditor(s_custom);
        }

        ImGui::Separator();
        if (ImGui::Button("Start Fight", ImVec2(180.0f, 0.0f)))
            startSelectedEncounter();
        if (s_status[0]) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s", s_status);
        }
    }
    ImGui::End();
}

void Tick()
{
    if (!s_validateLaunch || s_validateBoss < 0 || s_validateBoss >= kBossCount)
        return;
    const BossDefinition& boss = kBosses[s_validateBoss];
    if (++s_validateTotalFrames >= 900) {
        releaseOpeningLookStatus();
        cDmr_setSkipInfo(0);
        snprintf(s_status, sizeof(s_status),
                 "%s did not reach its target room. Check log.txt.", boss.name);
        Logger::LogWarn(
            "[tphd_tools][boss-practice] %s launch timed out before validation",
            boss.name);
        s_validateLaunch = false;
        return;
    }
    if (strncmp(dStage_getStageName(), boss.stage, DSTAGE_NAME_LEN) != 0 ||
        dStage_getRoomNo() != boss.room) {
        if (s_openingStatusCaptured)
            releaseOpeningLookStatus();
        return;
    }

    fopAc_ac_c* bossActor = fopAcM_searchByName(boss.expectedProc);
    if (bossActor) {
        if (s_triggerOpeningLook) {
            volatile u8* actorBytes = (volatile u8*)bossActor;
            const int action = *(volatile int*)(actorBytes + DEKU_TOAD_OFF_ACTION);
            const int mode = *(volatile int*)(actorBytes + DEKU_TOAD_OFF_MODE);
            if (action != DEKU_TOAD_ACT_OPENING) {
                releaseOpeningLookStatus();
                snprintf(s_status, sizeof(s_status),
                         "%s loaded in action %d instead of its opening. Check log.txt.",
                         boss.name, action);
                Logger::LogWarn(
                    "[tphd_tools][boss-practice] %s expected ACT_OPENING, got action=%d mode=%d",
                    boss.name, action, mode);
                s_validateLaunch = false;
                return;
            }

            if (!s_openingShutterClosed) {
                fopAc_ac_c* shutter =
                    fopAcM_searchByName(FPCNM_OBJ_AMISHUTTER);
                if (!shutter)
                    return;
                if (!daAmiShutter_setClosed(shutter)) {
                    releaseOpeningLookStatus();
                    snprintf(s_status, sizeof(s_status),
                             "%s grate had an unexpected actor type. Check log.txt.",
                             boss.name);
                    Logger::LogWarn(
                        "[tphd_tools][boss-practice] %s could not enter the verified closed-grate state",
                        boss.name);
                    s_validateLaunch = false;
                    return;
                }
                s_openingShutterClosed = true;
                Logger::Log(
                    "[tphd_tools][boss-practice] %s grate placed in native post-close state",
                    boss.name);
            }

            // Once mode 3 is visible, E_DT itself has stopped the camera, hidden
            // Link, started Deku Toad's sub-BGM, set one-zone switch 3, and will
            // order the potential event on its next native execute pass.
            if (mode < 3) {
                volatile u32* status =
                    (volatile u32*)GAME_ADDR_playerStatus0;
                if (!s_openingStatusCaptured) {
                    s_openingStatusWasSet =
                        (*status & DPLY_STATUS0_ZTARGET) != 0;
                    s_openingStatusCaptured = true;
                }
                *status |= DPLY_STATUS0_ZTARGET;
                u32 camera = *(volatile u32*)GAME_ADDR_cameraPtr;
                if (camera)
                    *(volatile int*)(camera + DCAM_OFF_MODE) = DCAM_MODE_SUBJECT;
                return;
            }

            releaseOpeningLookStatus();
            snprintf(s_status, sizeof(s_status),
                     "%s loaded; opening cinematic triggered natively.", boss.name);
        } else {
            snprintf(s_status, sizeof(s_status), "%s loaded; boss actor verified.",
                     boss.name);
        }
        Logger::Log("[tphd_tools][boss-practice] %s actor verified in %s room %d",
                    boss.name, boss.stage, (int)boss.room);
        s_validateLaunch = false;
        return;
    }

    if (++s_validateFrames >= 300) {
        releaseOpeningLookStatus();
        cDmr_setSkipInfo(0);
        snprintf(s_status, sizeof(s_status),
                 "%s room loaded, but its boss actor was not created. Check log.txt.",
                 boss.name);
        Logger::LogWarn(
            "[tphd_tools][boss-practice] %s actor missing after 300 target-room frames",
            boss.name);
        s_validateLaunch = false;
    }
}

void OnApplicationStart()
{
    // Aroma application starts can follow a destroyed game process. Drop our
    // bookkeeping without writing the previous process's player-status bit.
    s_openingStatusCaptured = false;
    s_openingStatusWasSet = false;
    s_validateLaunch = false;
    s_validateFrames = 0;
    s_validateTotalFrames = 0;
    s_validateBoss = -1;
    s_triggerOpeningLook = false;
    s_openingShutterClosed = false;
    s_status[0] = '\0';
}

} // namespace BossPractice
} // namespace Tools
