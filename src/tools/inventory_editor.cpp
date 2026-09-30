#include "tools/inventory_editor.h"

#include "tools/item_table.h"
#include "ui/menu_nav.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_save.h"

#include "imgui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace InventoryEditor {
static const char kWindowName[] = "Inventory Editor";
static const float kComboWidth = 260.0f;

static bool s_open = false;

void DrawMenuItem()
{
    Ui::Window::Checkbox("Inventory Editor", &s_open, kWindowName);
}

static void editU16(const char* label, volatile u16* p)
{
    uint16_t v = *p;
    if (Ui::Field::U16(label, &v))
        *p = v;
}

static void editU8(const char* label, volatile u8* p)
{
    uint8_t v = *p;
    if (Ui::Field::U8(label, &v))
        *p = v;
}

static void editName(const char* label, u32 off)
{
    char buf[dSv_NAME_SIZE];
    volatile u8* src = dComIfGs_saveByte(off);
    for (unsigned i = 0; i < dSv_NAME_SIZE; ++i)
        buf[i] = (char)src[i];
    buf[dSv_NAME_SIZE - 1] = '\0';
    if (Ui::Field::Text(label, buf, dSv_NAME_SIZE)) {
        for (unsigned i = 0; i < dSv_NAME_SIZE; ++i)
            src[i] = (u8)buf[i];
        src[dSv_NAME_SIZE - 1] = 0;
    }
}

static void bitCheck(const char* label, u32 off, int bit)
{
    bool v = dComIfGs_getSaveBitAt(off, bit);
    if (ImGui::Checkbox(label, &v))
        dComIfGs_setSaveBitAt(off, bit, v);
}

// Stamps are one big-endian u64, so stamps 0-31 live in the second word.
static bool hasStamp(int index)
{
    volatile u32* w = (volatile u32*)dComIfGs_saveByte(dSv_OFF_STAMP);
    if (index < 32)
        return (w[1] >> index) & 1u;
    return (w[0] >> (index - 32)) & 1u;
}

static void setStamp(int index, bool on)
{
    volatile u32* w = (volatile u32*)dComIfGs_saveByte(dSv_OFF_STAMP);
    volatile u32* word = index < 32 ? &w[1] : &w[0];
    const u32 mask = 1u << (index < 32 ? index : index - 32);
    if (on)
        *word |= mask;
    else
        *word &= ~mask;
}

static bool hasLetter(int index)
{
    volatile u32* w = (volatile u32*)dComIfGs_saveByte(dSv_OFF_LETTER_GET);
    return (w[index >> 5] & (1u << (index & 31))) != 0;
}

static void setLetter(int index, bool on)
{
    volatile u32* bits = (volatile u32*)dComIfGs_saveByte(dSv_OFF_LETTER_GET);
    const u32 mask = 1u << (index & 31);
    if (on)
        bits[index >> 5] |= mask;
    else
        bits[index >> 5] &= ~mask;

    volatile u8* order = dComIfGs_saveByte(dSv_OFF_LETTER_NUM);
    const u8 id = (u8)(index + 1);
    if (on) {
        for (int i = 0; i < 64; ++i)
            if (order[i] == id)
                return;
        for (int i = 0; i < 64; ++i) {
            if (order[i] == 0) {
                order[i] = id;
                return;
            }
        }
        return;
    }
    int out = 0;
    for (int i = 0; i < 64; ++i)
        if (order[i] != 0 && order[i] != id)
            order[out++] = order[i];
    for (int i = out; i < 64; ++i)
        order[i] = 0;
}

struct Choice {
    u8          id;
    const char* name;
};

static void byteCombo(const char* label, volatile u8* p, const Choice* opts, int n)
{
    const u8 cur = *p;
    const char* curName = "Unknown";
    for (int i = 0; i < n; ++i) {
        if (opts[i].id == cur) {
            curName = opts[i].name;
            break;
        }
    }
    ImGui::SetNextItemWidth(kComboWidth);
    if (ImGui::BeginCombo(label, curName)) {
        for (int i = 0; i < n; ++i)
            if (ImGui::Selectable(opts[i].name, opts[i].id == cur))
                *p = opts[i].id;
        ImGui::EndCombo();
    }
}

static void itemLabel(char* out, int n, u8 id)
{
    if (id == dItem_NONE) {
        snprintf(out, n, "None");
        return;
    }
    const char* name = kItemTable[id].name;
    if (!name || strcmp(name, "Reserved") == 0)
        snprintf(out, n, "Reserved (%u)", (unsigned)id);
    else
        snprintf(out, n, "%s", name);
}

static const u8 kBottle[] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x6A, 0x6B, 0x6C,
    0x73, 0x74, 0x76, 0x9E,
    0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x9F,
    0x7D, 0x7E, 0x7F,
};
static const u8 kBomb[]       = { 0x70, 0x71, 0x72 };
static const u8 kOoccoo[]     = { 0x25, 0x27, 0x33, 0xEC };
static const u8 kRod[]        = { 0x4A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F };
static const u8 kSkyBook[]    = { 0xE9, 0xEA, 0xEB };
static const u8 kLantern[]    = { 0x48, 0xF8 };
static const u8 kDominion[]   = { 0x4C, 0x46 };
static const u8 kGhost[]      = { 0xE8 };
static const u8 kBoomerang[]  = { 0x40 };
static const u8 kSpinner[]    = { 0x41 };
static const u8 kIronBoots[]  = { 0x45 };
static const u8 kBow[]        = { 0x43 };
static const u8 kHawkeye[]    = { 0x3E };
static const u8 kBallChain[]  = { 0x42 };
static const u8 kClawshot[]   = { 0x44 };
static const u8 kClawshots[]  = { 0x47 };
static const u8 kHorseCall[]  = { 0x84 };
static const u8 kSlingshot[]  = { 0x4B };

struct SlotDef {
    const char* name;
    const u8*   ids;
    int         count;
};

template <int N>
static constexpr SlotDef slot(const char* name, const u8 (&ids)[N])
{
    return { name, ids, N };
}

static const int kSlotCount = 24;

static const SlotDef kSlots[kSlotCount] = {
    slot("Gale Boomerang",   kBoomerang),
    slot("Lantern",          kLantern),
    slot("Spinner",          kSpinner),
    slot("Iron Boots",       kIronBoots),
    slot("Hero's Bow",       kBow),
    slot("Hawkeye",          kHawkeye),
    slot("Ball and Chain",   kBallChain),
    slot("Ghost Lantern",    kGhost),
    slot("Dominion Rod",     kDominion),
    slot("Clawshot",         kClawshot),
    slot("Double Clawshots", kClawshots),
    slot("Bottle 1",         kBottle),
    slot("Bottle 2",         kBottle),
    slot("Bottle 3",         kBottle),
    slot("Bottle 4",         kBottle),
    slot("Bomb Bag 1",       kBomb),
    slot("Bomb Bag 2",       kBomb),
    slot("Bomb Bag 3",       kBomb),
    slot("Ooccoo",           kOoccoo),
    { "Key Item", nullptr, 0 },
    slot("Fishing Rod",      kRod),
    slot("Horse Call",       kHorseCall),
    slot("Sky Book",         kSkyBook),
    slot("Slingshot",        kSlingshot),
};

static void drawStatusTab()
{
    volatile dSv_player_status_a_c* s = dComIfGs_getStatusA();

    editName("Player Name", dSv_OFF_PLAYER_NAME);
    editName("Horse Name", dSv_OFF_HORSE_NAME);
    editU16("Health", &s->mLife);
    editU16("Max Health", &s->mMaxLife);

    const int curHearts = s->mLife / 4;
    const int curQuarter = s->mLife % 4;
    const int maxHearts = s->mMaxLife / 5;
    const int heartPieces = s->mMaxLife % 5;
    char quarter[8] = "";
    char pieces[24] = "";
    if (curQuarter)
        snprintf(quarter, sizeof(quarter), " %d/4", curQuarter);
    if (heartPieces)
        snprintf(pieces, sizeof(pieces), ", %d of 5 pieces", heartPieces);
    ImGui::TextDisabled("%d%s of %d hearts%s", curHearts, quarter, maxHearts, pieces);

    editU16("Rupees", &s->mRupee);
    editU16("Lantern Oil", &s->mOil);
    editU16("Max Oil", &s->mMaxOil);

    ImGui::Separator();

    static const char* const kWallets[] = {
        "Wallet", "Big Wallet", "Giant Wallet", "Colossal Wallet",
    };
    int wallet = s->mWalletSize;
    if (wallet < 0 || wallet > 3)
        wallet = 0;
    ImGui::SetNextItemWidth(kComboWidth);
    if (ImGui::BeginCombo("Wallet", kWallets[wallet])) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kWallets[i], wallet == i))
                s->mWalletSize = (u8)i;
        ImGui::EndCombo();
    }

    const bool wolf = s->mTransformStatus != 0;
    ImGui::SetNextItemWidth(kComboWidth);
    if (ImGui::BeginCombo("Form", wolf ? "Wolf" : "Human")) {
        if (ImGui::Selectable("Human", !wolf))
            s->mTransformStatus = 0;
        if (ImGui::Selectable("Wolf", wolf))
            s->mTransformStatus = 1;
        ImGui::EndCombo();
    }
}

static void drawSlot(int index)
{
    const SlotDef& def = kSlots[index];
    const u8 cur = dComIfGs_getItemSlot(index);

    ImGui::PushID(index);
    if (def.count == 1) {
        bool have = cur == def.ids[0];
        if (ImGui::Checkbox(def.name, &have))
            dComIfGs_setItemSlot(index, have ? def.ids[0] : dItem_NONE);
        ImGui::PopID();
        return;
    }

    char curLabel[48];
    itemLabel(curLabel, sizeof(curLabel), cur);
    ImGui::SetNextItemWidth(kComboWidth);
    if (ImGui::BeginCombo(def.name, curLabel)) {
        if (ImGui::Selectable("None", cur == dItem_NONE))
            dComIfGs_setItemSlot(index, dItem_NONE);
        char label[48];
        if (def.count == 0) {
            for (int id = 0; id < 256; ++id) {
                if (!kItemTable[id].equip)
                    continue;
                itemLabel(label, sizeof(label), (u8)id);
                if (ImGui::Selectable(label, cur == id))
                    dComIfGs_setItemSlot(index, (u8)id);
            }
        } else {
            for (int i = 0; i < def.count; ++i) {
                const u8 id = def.ids[i];
                itemLabel(label, sizeof(label), id);
                if (ImGui::Selectable(label, cur == id))
                    dComIfGs_setItemSlot(index, id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

static void drawItemsTab()
{
    if (ImGui::Button("Clear All")) {
        for (int i = 0; i < kSlotCount; ++i)
            dComIfGs_setItemSlot(i, dItem_NONE);
    }
    ImGui::Separator();

    ImGui::BeginChild("##items");
    for (int i = 0; i < kSlotCount; ++i)
        drawSlot(i);
    ImGui::EndChild();
}

static void drawAmountsTab()
{
    volatile dSv_player_item_record_c* r = dComIfGs_getItemRecord();
    volatile dSv_player_item_max_c* m = dComIfGs_getItemMax();

    ImGui::SeparatorText("Amounts");
    editU8("Arrows", &r->mArrowNum);
    editU8("Slingshot Seeds", &r->mPachinkoNum);
    editU8("Bomb Bag 1", &r->mBombNum[0]);
    editU8("Bomb Bag 2", &r->mBombNum[1]);
    editU8("Bomb Bag 3", &r->mBombNum[2]);

    ImGui::SeparatorText("Max Capacities");
    editU8("Max Arrows", &m->mItemMax[dSv_MAX_ARROW]);
    editU8("Max Bombs", &m->mItemMax[dSv_MAX_NORMAL_BOMB]);
    editU8("Max Water Bombs", &m->mItemMax[dSv_MAX_WATER_BOMB]);
    editU8("Max Bomblings", &m->mItemMax[dSv_MAX_POKE_BOMB]);
}

static int s_gotIds[256];
static int s_gotCount = -1;

static void drawObtainedTab()
{
    if (s_gotCount < 0) {
        s_gotCount = 0;
        for (int id = 0; id < 256; ++id) {
            const char* name = kItemTable[id].name;
            if (id != dItem_NONE && name && strcmp(name, "Reserved") != 0)
                s_gotIds[s_gotCount++] = id;
        }
    }

    ImGui::BeginChild("##obtained");
    ImGuiListClipper clipper;
    clipper.Begin(s_gotCount);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int id = s_gotIds[row];
            bool got = dComIfGs_isItemFirstBit((u8)id);
            ImGui::PushID(id);
            if (ImGui::Checkbox(kItemTable[id].name, &got))
                dComIfGs_setItemFirstBit((u8)id, got);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

static void drawEquipmentTab()
{
    volatile dSv_player_status_a_c* s = dComIfGs_getStatusA();

    static const Choice kArmor[] = {
        { 46, "Ordon Clothes" }, { 47, "Hero's Clothes" }, { 48, "Magic Armor" },
        { 49, "Zora Armor" },
    };
    static const Choice kSword[] = {
        { 255, "None" }, { 40, "Ordon Sword" }, { 41, "Master Sword" },
        { 73, "Master Sword (Light)" }, { 63, "Wooden Sword" },
    };
    static const Choice kShield[] = {
        { 255, "None" }, { 42, "Ordon Shield" }, { 43, "Wooden Shield" },
        { 44, "Hylian Shield" },
    };
    static const Choice kScent[] = {
        { 255, "None" }, { 176, "Ilia" }, { 178, "Poe" }, { 179, "Reekfish" },
        { 180, "Youth's" }, { 181, "Medicine" },
    };

    ImGui::SeparatorText("Equipped");
    byteCombo("Clothes", &s->mSelectEquip[dSv_EQUIP_ARMOR], kArmor, IM_ARRAYSIZE(kArmor));
    byteCombo("Sword", &s->mSelectEquip[dSv_EQUIP_SWORD], kSword, IM_ARRAYSIZE(kSword));
    byteCombo("Shield", &s->mSelectEquip[dSv_EQUIP_SHIELD], kShield, IM_ARRAYSIZE(kShield));
    byteCombo("Scent", &s->mSelectEquip[dSv_EQUIP_SCENT], kScent, IM_ARRAYSIZE(kScent));

    ImGui::SeparatorText("Owned Gear");
    bitCheck("Hero's Clothes", dSv_OFF_HAVE_SWDSHLD, 7);
    bitCheck("Zora Armor", dSv_OFF_HAVE_ARMOR, 1);
    bitCheck("Magic Armor", dSv_OFF_HAVE_ARMOR, 0);
    ImGui::Spacing();
    bitCheck("Ordon Sword", dSv_OFF_HAVE_SWDSHLD, 0);
    bitCheck("Master Sword", dSv_OFF_HAVE_SWDSHLD, 1);
    bitCheck("Master Sword (Light)", dSv_OFF_HAVE_MSLIGHT, 1);
    ImGui::Spacing();
    bitCheck("Ordon Shield", dSv_OFF_HAVE_SWDSHLD, 2);
    bitCheck("Wooden Shield", dSv_OFF_HAVE_SWDSHLD, 3);
    bitCheck("Hylian Shield", dSv_OFF_HAVE_SWDSHLD, 4);
}

static const char* const kBugNames[12] = {
    "Snail", "Dragonfly", "Ant", "Dayfly",
    "Phasmid", "Pill Bug", "Mantis", "Ladybug",
    "Beetle", "Butterfly", "Stag Beetle", "Grasshopper",
};

static void setAllBytes(u32 off, int count, u8 value)
{
    for (int i = 0; i < count; ++i)
        *dComIfGs_saveByte(off + i) = value;
}

static void drawBugs()
{
    if (ImGui::SmallButton("All Bugs"))
        setAllBytes(dSv_OFF_BUGS, 3, 0xFF);
    ImGui::SameLine();
    if (ImGui::SmallButton("No Bugs"))
        setAllBytes(dSv_OFF_BUGS, 3, 0x00);

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##bugs", 3, flags))
        return;
    ImGui::TableSetupColumn("Species", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Male", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Female", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < 12; ++i) {
        const u32 off = dSv_OFF_BUGS + (u32)(i / 4);
        const int maleBit = (i % 4) * 2;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kBugNames[i]);
        ImGui::TableNextColumn();
        bitCheck("##m", off, maleBit);
        ImGui::TableNextColumn();
        bitCheck("##f", off, maleBit + 1);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

typedef bool (*HasFn)(int index);
typedef void (*SetFn)(int index, bool on);

static void drawToggleGrid(const char* id, const char* labelFormat, int count,
                           HasFn has, SetFn set)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchSame;
    if (!ImGui::BeginTable(id, 2, flags))
        return;
    for (int i = 0; i < count; ++i) {
        ImGui::TableNextColumn();
        ImGui::PushID(i);
        bool on = has(i);
        char label[24];
        snprintf(label, sizeof(label), labelFormat, i + 1);
        if (ImGui::Checkbox(label, &on))
            set(i, on);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

static void drawCollectionTab()
{
    ImGui::SeparatorText("Fused Shadows");
    bitCheck("Forest Temple", dSv_OFF_FUSED_SHADOW, 0);
    bitCheck("Goron Mines", dSv_OFF_FUSED_SHADOW, 1);
    bitCheck("Lakebed Temple", dSv_OFF_FUSED_SHADOW, 2);
    bitCheck("Twilight", dSv_OFF_FUSED_SHADOW, 3);

    ImGui::SeparatorText("Mirror Shards");
    bitCheck("Gerudo Desert", dSv_OFF_MIRROR, 0);
    bitCheck("Snowpeak", dSv_OFF_MIRROR, 1);
    bitCheck("Temple of Time", dSv_OFF_MIRROR, 2);
    bitCheck("City in the Sky", dSv_OFF_MIRROR, 3);

    ImGui::SeparatorText("Poe Souls");
    int poe = *dComIfGs_saveByte(dSv_OFF_POE_SOULS);
    if (Ui::Field::SliderInt("Souls", &poe, 0, 60))
        *dComIfGs_saveByte(dSv_OFF_POE_SOULS) = (u8)poe;

    ImGui::SeparatorText("Hidden Skills");
    bitCheck("Ending Blow", dSv_OFF_SKILL_LO, 2);
    bitCheck("Shield Attack", dSv_OFF_SKILL_LO, 3);
    bitCheck("Back Slice", dSv_OFF_SKILL_LO, 1);
    bitCheck("Helm Splitter", dSv_OFF_SKILL_LO, 0);
    bitCheck("Mortal Draw", dSv_OFF_SKILL_HI, 7);
    bitCheck("Jump Strike", dSv_OFF_SKILL_HI, 6);
    bitCheck("Great Spin Attack", dSv_OFF_SKILL_HI, 5);

    ImGui::SeparatorText("Golden Bugs");
    drawBugs();

    ImGui::SeparatorText("Stamps");
    if (ImGui::SmallButton("All Stamps")) {
        for (int i = 0; i < dSv_STAMP_COUNT; ++i)
            setStamp(i, true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("No Stamps")) {
        for (int i = 0; i < dSv_STAMP_COUNT; ++i)
            setStamp(i, false);
    }
    drawToggleGrid("##stamps", "Stamp %02d", dSv_STAMP_COUNT, hasStamp, setStamp);

    ImGui::SeparatorText("Mail");
    if (ImGui::SmallButton("All Mail")) {
        for (int i = 0; i < dSv_LETTER_COUNT; ++i)
            setLetter(i, true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("No Mail")) {
        for (int i = 0; i < dSv_LETTER_COUNT; ++i)
            setLetter(i, false);
    }
    drawToggleGrid("##mail", "Letter %02d", dSv_LETTER_COUNT, hasLetter, setLetter);
}

void DrawWindow()
{
    if (!s_open)
        return;

    ImGui::SetNextWindowPos(ImVec2(200.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(540.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (!dComIfGp_getPlayer()) {
            ImGui::TextDisabled("Load a save first.");
        } else if (ImGui::BeginTabBar("##inventory")) {
            Ui::Nav::RegisterCurrentTabBar();
            if (ImGui::BeginTabItem("Player"))         { drawStatusTab();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Items"))          { drawItemsTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Amounts"))        { drawAmountsTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Equipment"))      { drawEquipmentTab();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Obtained Items")) { drawObtainedTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Collection"))     { drawCollectionTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
}
}
