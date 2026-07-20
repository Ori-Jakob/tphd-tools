// inventory_editor.cpp -- see inventory_editor.h.
//
// A live save editor (modeled on dusklight's ImGuiSaveEditor) scoped to the
// inventory + player status. All edits poke the persistent save block info @
// 0x10145348 via the typed views in game/d_inventory.h. Item-wheel changes go
// through the game's setItem so the lineup + equipped slots refresh.
#include "cheats/inventory_editor.h"

#include "imgui.h"
#include "game/game.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>

namespace Cheats {
namespace InventoryEditor {

static bool s_open = false;

void DrawMenuItem()
{
    ImGui::Checkbox("Inventory Editor", &s_open);
}
bool IsOpen()         { return s_open; }
void SetOpen(bool o)  { s_open = o; }

// ---- small typed-edit helpers ----------------------------------------------
static void editU16(const char* label, volatile u16* p)
{
    u16 v = *p;
    if (ImGui::InputScalar(label, ImGuiDataType_U16, &v))
        *p = v;
}
static void editU8(const char* label, volatile u8* p)
{
    u8 v = *p;
    if (ImGui::InputScalar(label, ImGuiDataType_U8, &v))
        *p = v;
}

// A checkbox bound to a single bit at (off, bit) in the save block.
static void bitCheck(const char* label, u32 off, int bit)
{
    bool v = dInv_getBit(off, bit);
    if (ImGui::Checkbox(label, &v))
        dInv_setBit(off, bit, v);
}

struct ItemChoice { u8 id; const char* name; };

// A combo that writes one of `opts`' ids into the byte at *p.
static void byteCombo(const char* label, volatile u8* p, const ItemChoice* opts, int n)
{
    u8 cur = *p;
    const char* curName = "Unknown";
    for (int i = 0; i < n; ++i)
        if (opts[i].id == cur) { curName = opts[i].name; break; }
    if (ImGui::BeginCombo(label, curName)) {
        for (int i = 0; i < n; ++i)
            if (ImGui::Selectable(opts[i].name, opts[i].id == cur))
                *p = opts[i].id;
        ImGui::EndCombo();
    }
}

// Item display name; falls back to "Reserved (id)" for unnamed/reserved ids so
// unknown items (e.g. the TPHD Ghost Lantern) can still be identified.
static void itemLabel(char* out, int n, u8 id)
{
    if (id == DSV_ITEM_NONE) { snprintf(out, n, "None"); return; }
    const char* nm = kItemTable[id].name;
    if (!nm || strcmp(nm, "Reserved") == 0)
        snprintf(out, n, "Reserved (%u)", id);
    else
        snprintf(out, n, "%s", nm);
}

// ---- per-slot allowed items (TP places each item in a fixed pouch slot) ------
// Item ids are dItemNo_*_e from d_item_data.h.
static const u8 kBottle[] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,   // empty / potions / milk / oil / water
    0x6A, 0x6B, 0x6C,                                 // nasty soup / hot springwater / fairy
    0x73, 0x74, 0x76, 0x9E,                           // great-fairy tears / worm / bee larvae (x2)
    0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x9F,         // chu jellies (+ black)
    0x7D, 0x7E, 0x7F,                                 // soups
};
static const u8 kBomb[]    = { 0x70, 0x71, 0x72 };                         // bombs / water / bomblings
static const u8 kOoccoo[]  = { 0x25, 0x27, 0x33, 0xEC };                   // Ooccoo variants
static const u8 kRod[]     = { 0x4A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F };       // fishing rod variants
static const u8 kSkyBook[] = { 0xE9, 0xEA, 0xEB };                         // sky book stages
static const u8 kLantern[] = { 0x48, 0xF8 };                              // lantern / reclaimed
static const u8 kRod8[]    = { 0x4C, 0x46 };                              // dominion rod / unusable
static const u8 kGhost[]   = { 0xE8 };                                    // Ghost Lantern (TPHD)
static const u8 kBoomerang[] = { 0x40 }; static const u8 kSpinner[]  = { 0x41 };
static const u8 kIronBoots[] = { 0x45 }; static const u8 kBow[]      = { 0x43 };
static const u8 kHawkeye[]   = { 0x3E }; static const u8 kBallChain[]= { 0x42 };
static const u8 kClawshot[]  = { 0x44 }; static const u8 kWClawshot[]= { 0x47 };
static const u8 kHorseCall[] = { 0x84 }; static const u8 kSlingshot[]= { 0x4B };

// Each slot carries a label (the item / category it holds). count==1 renders a
// have/don't-have checkbox; count>1 (or 0 = open/full-list) renders a dropdown.
typedef struct SlotDef { const char* name; const u8* ids; int count; } SlotDef;
#define SD(nm, a)  { nm, a, (int)(sizeof(a) / sizeof((a)[0])) }
#define OPEN(nm)   { nm, 0, 0 }
static const SlotDef kSlots[24] = {
    SD("Gale Boomerang",   kBoomerang),  // 00
    SD("Lantern",          kLantern),    // 01
    SD("Spinner",          kSpinner),    // 02
    SD("Iron Boots",       kIronBoots),  // 03
    SD("Hero's Bow",       kBow),        // 04
    SD("Hawkeye",          kHawkeye),    // 05
    SD("Ball and Chain",   kBallChain),  // 06
    SD("Ghost Lantern",    kGhost),      // 07
    SD("Dominion Rod",     kRod8),       // 08
    SD("Clawshot",         kClawshot),   // 09
    SD("Double Clawshots", kWClawshot),  // 10
    SD("Bottle",           kBottle),     // 11
    SD("Bottle",           kBottle),     // 12
    SD("Bottle",           kBottle),     // 13
    SD("Bottle",           kBottle),     // 14
    SD("Bomb Bag",         kBomb),       // 15
    SD("Bomb Bag",         kBomb),       // 16
    SD("Bomb Bag",         kBomb),       // 17
    SD("Ooccoo",           kOoccoo),     // 18
    OPEN("Key Item"),                    // 19
    SD("Fishing Rod",      kRod),        // 20
    SD("Horse Call",       kHorseCall),  // 21
    SD("Sky Book",         kSkyBook),    // 22
    SD("Slingshot",        kSlingshot),  // 23
};
#undef SD
#undef OPEN

// ---- tabs -------------------------------------------------------------------
static void drawStatusTab()
{
    volatile dSv_status_a_c* s = g_svStatusA;

    editU16("Health",      &s->mLife);
    editU16("Max Health",  &s->mMaxLife);

    int curHearts   = s->mLife    / 0x04;
    int curQuarter  = s->mLife    % 0x04;
    int maxHearts   = s->mMaxLife / 0x05;
    int heartPieces = s->mMaxLife % 0x05;

    // Pre-format the optional "/4" so it only shows on a partial heart, then let
    // the printf-style TextDisabled assemble the line.
    char quarter[10] = "";
    char pieces[12] = "";
    if (curQuarter)
        snprintf(quarter, sizeof(quarter), ", %d/4", curQuarter);
    if (heartPieces)
        snprintf(pieces, sizeof(pieces), ", %d Pieces", heartPieces);

    ImGui::TextDisabled("Current = %d%s Heart(s)\nMax = %d Hearts%s",
                        curHearts, quarter, maxHearts, pieces);
    editU16("Rupees",      &s->mRupee);
    editU16("Lantern Oil", &s->mOil);
    editU16("Max Oil",     &s->mMaxOil);

    ImGui::Separator();

    static const char* kWallet[] = { "Wallet", "Big Wallet", "Giant Wallet", "Colossal Wallet" };
    int wallet = s->mWalletSize;
    if (wallet < 0 || wallet > 3) wallet = 0;
    if (ImGui::BeginCombo("Wallet", kWallet[wallet])) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kWallet[i], wallet == i))
                s->mWalletSize = (u8)i;
        ImGui::EndCombo();
    }

    int form = s->mTransformStatus ? 1 : 0;
    if (ImGui::BeginCombo("Form", form ? "Wolf" : "Human")) {
        if (ImGui::Selectable("Human", form == 0)) s->mTransformStatus = 0;
        if (ImGui::Selectable("Wolf",  form == 1)) s->mTransformStatus = 1;
        ImGui::EndCombo();
    }
}

static void drawSlot(int slot)
{
    const SlotDef& def = kSlots[slot];
    u8 cur = dInv_getItem(slot);

    ImGui::PushID(slot);

    // Single fixed item -> a simple have/don't-have checkbox labeled with it.
    if (def.count == 1) {
        bool have = (cur == def.ids[0]);
        if (ImGui::Checkbox(def.name, &have))
            dInv_setItem(slot, have ? def.ids[0] : DSV_ITEM_NONE);
        ImGui::PopID();
        return;
    }

    // Variant / open slot -> a wide dropdown labeled (on its right) with the
    // slot's category name. 'None' empties it, so no separate Clear button.
    char cur_lbl[48];
    itemLabel(cur_lbl, sizeof(cur_lbl), cur);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo(def.name, cur_lbl)) {
        if (ImGui::Selectable("None", cur == DSV_ITEM_NONE))
            dInv_setItem(slot, DSV_ITEM_NONE);
        if (def.count == 0) {                  // open slot: every equippable item
            for (int id = 0; id < 256; ++id) {
                if (!kItemTable[id].equip)
                    continue;
                char lbl[48];
                itemLabel(lbl, sizeof(lbl), (u8)id);
                if (ImGui::Selectable(lbl, cur == id))
                    dInv_setItem(slot, (u8)id);
            }
        } else {                               // constrained to this slot's variants
            for (int i = 0; i < def.count; ++i) {
                u8 id = def.ids[i];
                char lbl[48];
                itemLabel(lbl, sizeof(lbl), id);
                if (ImGui::Selectable(lbl, cur == id))
                    dInv_setItem(slot, id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

static void drawItemsTab()
{
    if (ImGui::Button("Clear All")) {
        for (int slot = 0; slot < 24; ++slot)
            dInv_setItem(slot, DSV_ITEM_NONE);
    }
    ImGui::TextDisabled("Each slot is limited to the item(s) it can hold.");
    ImGui::Separator();

    ImGui::BeginChild("##items", ImVec2(0, 0), false);
    for (int slot = 0; slot < 24; ++slot)
        drawSlot(slot);
    ImGui::EndChild();
}

static void drawAmountsTab()
{
    volatile dSv_itemRecord_c* r = g_svItemRecord;
    volatile dSv_itemMax_c*    m = g_svItemMax;

    ImGui::SeparatorText("Amounts");
    editU8("Arrows",          &r->mArrowNum);
    editU8("Slingshot Seeds", &r->mPachinkoNum);
    editU8("Bomb Bag 1",      &r->mBombNum[0]);
    editU8("Bomb Bag 2",      &r->mBombNum[1]);
    editU8("Bomb Bag 3",      &r->mBombNum[2]);

    ImGui::SeparatorText("Max Capacities");
    editU8("Max Arrows",     &m->mItemMax[DSV_MAX_ARROW]);
    editU8("Max Bombs",      &m->mItemMax[DSV_MAX_NORMAL_BOMB]);
    editU8("Max Water Bombs",&m->mItemMax[DSV_MAX_WATER_BOMB]);
    editU8("Max Bomblings",  &m->mItemMax[DSV_MAX_POKE_BOMB]);
}

static void drawGotItemsTab()
{
    ImGui::TextDisabled("Toggles the 'have ever obtained' flag for each item.");
    ImGui::BeginChild("##gotitems", ImVec2(0, 0), false);
    for (int id = 0; id < 256; ++id) {
        const char* n = kItemTable[id].name;
        if (!n || strcmp(n, "Reserved") == 0)
            continue;
        bool got = dInv_getItemFlag((u8)id);
        ImGui::PushID(id);
        if (ImGui::Checkbox(n, &got))
            dInv_setItemFlag((u8)id, got);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void drawEquipmentTab()
{
    volatile dSv_status_a_c* s = g_svStatusA;

    ImGui::SeparatorText("Equipped");
    static const ItemChoice kArmor[]  = {
        { 46, "Ordon Clothes" }, { 47, "Hero's Clothes" }, { 48, "Magic Armor" },
        { 49, "Zora Armor" },
    };
    static const ItemChoice kSword[]  = {
        { 255, "None" }, { 40, "Ordon Sword" }, { 41, "Master Sword" },
        { 73, "Master Sword (Light)" }, { 63, "Wooden Sword" },
    };
    static const ItemChoice kShield[] = {
        { 255, "None" }, { 42, "Ordon Shield" }, { 43, "Wooden Shield" },
        { 44, "Hylian Shield" },
    };
    static const ItemChoice kScent[]  = {
        { 255, "None" }, { 176, "Ilia" }, { 178, "Poe" }, { 179, "Reekfish" },
        { 180, "Youth's" }, { 181, "Medicine" },
    };
    byteCombo("Clothes", &s->mSelectEquip[DSV_EQUIP_ARMOR],  kArmor,  IM_ARRAYSIZE(kArmor));
    byteCombo("Sword",   &s->mSelectEquip[DSV_EQUIP_SWORD],  kSword,  IM_ARRAYSIZE(kSword));
    byteCombo("Shield",  &s->mSelectEquip[DSV_EQUIP_SHIELD], kShield, IM_ARRAYSIZE(kShield));
    byteCombo("Scent",   &s->mSelectEquip[DSV_EQUIP_SCENT],  kScent,  IM_ARRAYSIZE(kScent));

    ImGui::SeparatorText("Owned Gear");
    bitCheck("Hero's Clothes", DSV_OFF_HAVE_SWDSHLD, 7);   // 0x1014541A b7 (user-verified)
    bitCheck("Zora Armor",     DSV_OFF_HAVE_ARMOR, 1);
    bitCheck("Magic Armor",    DSV_OFF_HAVE_ARMOR, 0);
    ImGui::Spacing();
    bitCheck("Ordon Sword",         DSV_OFF_HAVE_SWDSHLD, 0);
    bitCheck("Master Sword",        DSV_OFF_HAVE_SWDSHLD, 1);
    bitCheck("Master Sword (Light)",DSV_OFF_HAVE_MSLIGHT, 1);
    ImGui::Spacing();
    bitCheck("Ordon Shield",  DSV_OFF_HAVE_SWDSHLD, 2);
    bitCheck("Wooden Shield", DSV_OFF_HAVE_SWDSHLD, 3);
    bitCheck("Hylian Shield", DSV_OFF_HAVE_SWDSHLD, 4);
}

// 12 golden-bug species in CE bit order: byte 0xE5+(i/4), male bit (i%4)*2, female +1.
static const char* kBugNames[12] = {
    "Snail", "Dragonfly", "Ant", "Dayfly",
    "Phasmid", "Pill Bug", "Mantis", "Ladybug",
    "Beetle", "Butterfly", "Stag Beetle", "Grasshopper",
};

static void drawCollectionTab()
{
    ImGui::SeparatorText("Fused Shadows");
    bitCheck("Forest Temple",  0x109u, 0);
    bitCheck("Goron Mines",    0x109u, 1);   // "Fire"
    bitCheck("Lakebed Temple", 0x109u, 2);   // "Water"
    bitCheck("Twilight",       0x109u, 3);

    ImGui::SeparatorText("Mirror Shards");
    bitCheck("Gerudo Desert",   0x10Au, 0);
    bitCheck("Snowpeak",        0x10Au, 1);
    bitCheck("Temple of Time",  0x10Au, 2);
    bitCheck("City in the Sky", 0x10Au, 3);

    ImGui::SeparatorText("Poe Souls");
    {
        int poe = *g_svPoeSouls;
        if (ImGui::SliderInt("Souls (max 60)", &poe, 0, 60))
            *g_svPoeSouls = (u8)poe;
    }

    ImGui::SeparatorText("Hidden Skills");
    bitCheck("Ending Blow",       DSV_OFF_SKILL_LO, 2);
    bitCheck("Shield Attack",     DSV_OFF_SKILL_LO, 3);
    bitCheck("Back Slice",        DSV_OFF_SKILL_LO, 1);
    bitCheck("Helm Splitter",     DSV_OFF_SKILL_LO, 0);
    bitCheck("Mortal Draw",       DSV_OFF_SKILL_HI, 7);
    bitCheck("Jump Strike",       DSV_OFF_SKILL_HI, 6);
    bitCheck("Great Spin Attack", DSV_OFF_SKILL_HI, 5);

    ImGui::SeparatorText("Golden Bugs");
    if (ImGui::SmallButton("All Bugs")) {
        for (int i = 0; i < 3; ++i) *dInv_byte(DSV_OFF_BUGS + i) = 0xFF;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("No Bugs")) {
        for (int i = 0; i < 3; ++i) *dInv_byte(DSV_OFF_BUGS + i) = 0x00;
    }
    if (ImGui::BeginTable("##bugs", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Species", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("M");
        ImGui::TableSetupColumn("F");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 12; ++i) {
            u32 off    = DSV_OFF_BUGS + (i / 4);
            int maleBit = (i % 4) * 2;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kBugNames[i]);
            ImGui::TableNextColumn(); bitCheck("##m", off, maleBit);
            ImGui::TableNextColumn(); bitCheck("##f", off, maleBit + 1);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---- events tab -------------------------------------------------------------
// Case-insensitive "haystack contains needle".
static bool ciContains(const char* hay, const char* needle)
{
    if (!needle[0])
        return true;
    for (const char* h = hay; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b &&
               (char)tolower((unsigned char)*a) == (char)tolower((unsigned char)*b)) {
            ++a; ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

static char s_evtFilter[64] = "";

static void drawEventsTab()
{
    ImGui::TextDisabled("Story / quest progression bits (mEvent). Toggling these can");
    ImGui::TextDisabled("advance or break cutscene state -- edit with care.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##evtfilter", "search name / location / description",
                             s_evtFilter, sizeof(s_evtFilter));

    // Filtered index list (rebuilt each frame; cheap for ~800 entries).
    static int idx[kEventFlagCount];
    int n = 0;
    for (int i = 0; i < kEventFlagCount; ++i) {
        const EventFlagEntry& e = kEventFlags[i];
        if (ciContains(e.name, s_evtFilter) || ciContains(e.location, s_evtFilter) ||
            ciContains(e.desc, s_evtFilter))
            idx[n++] = i;
    }
    ImGui::Text("%d / %d flags", n, kEventFlagCount);

    ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                         ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
    if (ImGui::BeginTable("##events", 4, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On",   ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Sort the filtered list by the active column (re-sorted every frame so
        // the "On" column tracks live flag state, like dusklight does).
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsCount > 0) {
                const int  col = ss->Specs[0].ColumnIndex;
                const bool asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                std::sort(idx, idx + n, [col, asc](int a, int b) {
                    const EventFlagEntry& x = kEventFlags[a];
                    const EventFlagEntry& y = kEventFlags[b];
                    int c;
                    switch (col) {
                    case 0:  c = (int)dEvent_get(x.byteIndex, x.mask) -
                                 (int)dEvent_get(y.byteIndex, y.mask); break;
                    case 1:  c = strcmp(x.name, y.name);         break;
                    case 2:  c = strcmp(x.location, y.location); break;
                    default: c = strcmp(x.desc, y.desc);         break;
                    }
                    if (c == 0) c = a - b;   // stable tiebreak by table order
                    return asc ? (c < 0) : (c > 0);
                });
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(n);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const EventFlagEntry& e = kEventFlags[idx[row]];
                ImGui::PushID(idx[row]);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool on = dEvent_get(e.byteIndex, e.mask);
                if (ImGui::Checkbox("##on", &on))
                    dEvent_set(e.byteIndex, e.mask, on);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(e.name);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(e.location);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(e.desc);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

// ---- area (scene) flags tab -------------------------------------------------
// A collapsible grid of raw bits (chests / switches / items), labelled by index.
static void drawAreaFlagGrid(const char* label, volatile u32* arr, int words)
{
    if (!ImGui::TreeNode(label))
        return;
    int total = words * 32;
    for (int i = 0; i < total; ++i) {
        if ((i & 15) == 0) ImGui::Text("%3d", i);   // row label every 16
        ImGui::SameLine();
        ImGui::PushID(i);
        bool on = dMem_getBit(arr, i);
        if (ImGui::Checkbox("##b", &on))
            dMem_setBit(arr, i, on);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// A byte-oriented bit grid (8 checkboxes per row, labelled by byte index), for raw
// blocks that aren't clean u32 flag arrays. Reads/writes `bytes` live each frame.
static void drawByteBitGrid(const char* label, volatile u8* bytes, int count)
{
    if (!ImGui::TreeNode(label))
        return;
    for (int b = 0; b < count; ++b) {
        ImGui::Text("%2d", b);          // byte index (matches a raw memory view)
        for (int bit = 0; bit < 8; ++bit) {
            ImGui::SameLine();
            ImGui::PushID(b * 8 + bit);
            bool on = (bytes[b] >> bit) & 1u;
            if (ImGui::Checkbox("##e", &on)) {
                if (on) bytes[b] |= (u8)(1u << bit);
                else    bytes[b] &= (u8)~(1u << bit);
            }
            ImGui::PopID();
        }
    }
    ImGui::TreePop();
}

static void drawAreaTab()
{
    ImGui::TextDisabled("Scene flags for the area you're in NOW (temp storage).");
    ImGui::TextDisabled("Edits apply live; the game saves them to permanent on area change.");

    volatile dSv_memBit_c* m = g_areaTempBit;

    // Map/compass/boss-key/keys only mean something inside a dungeon (stage codes
    // start with "D_MN"); hide them elsewhere so they aren't toggled by mistake.
    bool inDungeon = strncmp(dStage_getStageName(), "D_MN", 4) == 0;

    if (inDungeon) {
        ImGui::SeparatorText("Dungeon Items");
        auto db = [&](const char* lbl, int n) {
            bool on = (m->mDungeonItem >> n) & 1u;
            if (ImGui::Checkbox(lbl, &on)) {
                if (on) m->mDungeonItem |= (u8)(1u << n);
                else    m->mDungeonItem &= (u8)~(1u << n);
            }
        };
        // Two evenly-spaced columns (a table avoids the SameLine label overlap).
        if (ImGui::BeginTable("##dungeonitems", 2, ImGuiTableFlags_SizingStretchSame)) {
            auto cell = [&](const char* lbl, int n) { ImGui::TableNextColumn(); db(lbl, n); };
            cell("Map",             DMEM_MAP);             cell("Compass",          DMEM_COMPASS);
            cell("Boss Key",        DMEM_BOSS_KEY);        cell("Boss Defeated",    DMEM_BOSS_DEFEATED);
            cell("Heart Container", DMEM_HEART_CONTAINER); cell("Boss Cutscene Seen", DMEM_BOSS_DEMO);
            cell("Ooccoo",          DMEM_OOCCOO);          cell("Miniboss Defeated", DMEM_MINIBOSS);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Small Keys");
        int k = m->mKeyNum;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Keys", &k)) {
            if (k < 0) k = 0; 
            if (k > 255) k = 255;
            m->mKeyNum = (u8)k;
        }
    } else {
        ImGui::SeparatorText("Dungeon Items");
        ImGui::TextDisabled("(only shown inside a dungeon -- D_MN map)");
    }

    ImGui::SeparatorText("Flag Matrix");
    drawAreaFlagGrid("Chests",   m->mTbox,   2);
    drawAreaFlagGrid("Switches", m->mSwitch, 4);
    drawAreaFlagGrid("Items",    m->mItem,   1);

    // Stage events: a 32-slot pool of 0x20-byte per-zone records (info+0xE54),
    // separate from the memBit above. Slots in use have their id byte (+0) set to
    // something other than 0xFF (the constructor stamps 0xFF = empty), so we can
    // auto-follow the in-use slot for the zone you're in instead of guessing a
    // region index. It's a pool, so more than one slot can be active at once.
    ImGui::SeparatorText("Stage Events");
    ImGui::TextDisabled("Per-zone scratch records at info+0xE54. Live, raw bits.");

    int firstActive = -1, activeCount = 0;
    for (int s = 0; s < DSV_STAGE_EVENT_SLOTS; ++s) {
        if (dStageEvent_slot(s)[0] != 0xFF) {
            if (firstActive < 0) firstActive = s;
            ++activeCount;
        }
    }

    static bool s_autoSlot = true;
    static int  s_eventSlot = 0;
    ImGui::Checkbox("Follow active slot", &s_autoSlot);
    if (s_autoSlot && firstActive >= 0)
        s_eventSlot = firstActive;
    if (!s_autoSlot) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Slot (0-31)", &s_eventSlot)) {
            if (s_eventSlot < 0) s_eventSlot = 0;
            if (s_eventSlot >= DSV_STAGE_EVENT_SLOTS) s_eventSlot = DSV_STAGE_EVENT_SLOTS - 1;
        }
    }
    volatile u8* ev = dStageEvent_slot(s_eventSlot);
    ImGui::Text("Slot %d  id=0x%02X %s   (%d in use)", s_eventSlot, (unsigned)ev[0],
                ev[0] == 0xFF ? "(empty)" : "(active)", activeCount);
    drawByteBitGrid("Event bits", ev, (int)DSV_STAGE_EVENT_STRIDE);
}

// ---- Midna warp-map unlock flags --------------------------------------------
// Each warp portal has a PERSISTENT unlock bit in the saved per-region memBit
// (the info+0x2F0 array; `persist` is its absolute address) and a LIVE switch bit
// in the current-area memBit (info+0xDF8) at the SAME offset within the 0x20
// struct. Setting the live bit only makes the portal appear -- and only means the
// right switch -- while you're standing in that warp's field stage, so we mirror
// to the live bit only when the current stage matches stageA/stageB. Addresses +
// bits are from the user's cheat table; the live address is derived, not stored:
//   live = memBitTemp + ((persist - memBitSave) % 0x20).
struct WarpFlag {
    const char* name;
    u32         persist;   // saved unlock bit, absolute address
    u8          bit;       // bit index 0..7
    const char* stageA;    // field stage where the portal is in the current zone
    const char* stageB;    // optional second stage, else nullptr
};

static const WarpFlag kWarpFlags[] = {
    { "Ordon Spring",       0x10145645u, 4, "F_SP104", nullptr   },
    { "S. Faron Woods",     0x1014568Bu, 7, "F_SP108", nullptr   },
    { "N. Faron Woods",     0x10145683u, 2, "F_SP108", nullptr   },
    { "Kakariko Gorge",     0x10145701u, 5, "F_SP121", nullptr   },
    { "Kakariko Village",   0x101456A0u, 7, "F_SP109", nullptr   },
    { "Death Mountain",     0x101456A1u, 5, "F_SP109", "F_SP110" },
    { "Bridge of Eldin",    0x1014570Fu, 3, "F_SP121", nullptr   },
    { "Zora's Domain",      0x101456C3u, 2, "F_SP113", nullptr   },
    { "Lake Hylia",         0x101456C2u, 2, "F_SP115", nullptr   },
    { "Castle Town",        0x10145703u, 3, "F_SP122", nullptr   },
    { "Upper Zora's River", 0x101456C1u, 5, "F_SP126", nullptr   },
    { "Gerudo Mesa",        0x10145781u, 5, "F_SP124", nullptr   },
    { "Mirror Chamber",     0x10145786u, 0, "F_SP125", nullptr   },
    { "Snowpeak Top",       0x10145741u, 5, "F_SP114", nullptr   },
    { "Sacred Grove",       0x1014572Fu, 4, "F_SP117", nullptr   },
};
static const int kWarpFlagCount = (int)(sizeof(kWarpFlags) / sizeof(kWarpFlags[0]));

static volatile u8* warpLiveByte(u32 persist)
{
    u32 off = (persist - GAME_ADDR_memBitSave) % DSV_MEMBIT_STRIDE;
    return (volatile u8*)(GAME_ADDR_memBitTemp + off);
}

static bool warpInCurrentStage(const WarpFlag& w)
{
    const char* s = dStage_getStageName();
    if (w.stageA && strncmp(s, w.stageA, DSTAGE_NAME_LEN) == 0) return true;
    if (w.stageB && strncmp(s, w.stageB, DSTAGE_NAME_LEN) == 0) return true;
    return false;
}

// Set/clear the persistent unlock bit, and mirror to the live switch when the
// warp's portal is in the stage we're standing in (so it appears immediately).
static void setWarpUnlocked(const WarpFlag& w, bool on, bool inZone)
{
    volatile u8* p = (volatile u8*)w.persist;
    if (on) *p |= (u8)(1u << w.bit);
    else    *p &= (u8)~(1u << w.bit);
    if (inZone) {
        volatile u8* live = warpLiveByte(w.persist);
        if (on) *live |= (u8)(1u << w.bit);
        else    *live &= (u8)~(1u << w.bit);
    }
}

static void drawWarpsTab()
{
    // const char* stage = dStage_getStageName();

    if (ImGui::BeginTable("##warps", 1,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Warp", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < kWarpFlagCount; ++i) {
            const WarpFlag& w = kWarpFlags[i];
            bool on = (*(volatile u8*)w.persist >> w.bit) & 1u;
            bool inZone = warpInCurrentStage(w);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (inZone)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            if (ImGui::Checkbox(w.name, &on))
                setWarpUnlocked(w, on, inZone);
            if (inZone)
                ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Unlock all")) {
        for (int i = 0; i < kWarpFlagCount; ++i)
            setWarpUnlocked(kWarpFlags[i], true, warpInCurrentStage(kWarpFlags[i]));
    }
    ImGui::SameLine();
    if (ImGui::Button("Lock all")) {
        for (int i = 0; i < kWarpFlagCount; ++i)
            setWarpUnlocked(kWarpFlags[i], false, warpInCurrentStage(kWarpFlags[i]));
    }
}

// ---- window -----------------------------------------------------------------
void DrawWindow(bool menuActive)
{
    if (!s_open)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(200.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(540.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inventory Editor", &s_open, flags)) {
        if (!dComIfGp_getPlayer()) {
            ImGui::TextDisabled("Load a game save first.");
        } else if (ImGui::BeginTabBar("##inveditor")) {
            if (ImGui::BeginTabItem("Player"))     { drawStatusTab();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Items"))      { drawItemsTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Amounts"))    { drawAmountsTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Equipment"))  { drawEquipmentTab();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Obtained Items"))  { drawGotItemsTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Collection")) { drawCollectionTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Dungeon & Area")) { drawAreaTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Warp Portals")) { drawWarpsTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Event Flags")) { drawEventsTab();    ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace InventoryEditor
} // namespace Cheats
