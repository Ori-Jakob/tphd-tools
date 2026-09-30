#include "tools/world_editor.h"

#include "tools/event_flags_table.h"
#include "ui/menu_nav.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "d/d_item.h"
#include "d/d_save.h"
#include "d/d_stage.h"

#include "imgui.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include <algorithm>

namespace Tools {
namespace WorldEditor {
static const char kWindowName[] = "World Editor";

static bool s_open = false;

void DrawMenuItem()
{
    Ui::Window::Checkbox("World Editor", &s_open, kWindowName);
}

static bool containsNoCase(const char* hay, const char* needle)
{
    if (!needle[0])
        return true;
    for (const char* h = hay; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

static char s_filter[64] = "";
static char s_cachedFilter[sizeof(s_filter)] = "";
static bool s_filterReady = false;
static int  s_order[kEventFlagCount];
static int  s_orderCount = 0;
static bool s_orderDirty = true;
static bool s_hadSortSpecs = false;

static void refilter()
{
    if (s_filterReady && strcmp(s_cachedFilter, s_filter) == 0)
        return;
    s_orderCount = 0;
    for (int i = 0; i < kEventFlagCount; ++i) {
        const EventFlagEntry& e = kEventFlags[i];
        if (containsNoCase(e.name, s_filter) || containsNoCase(e.location, s_filter) ||
            containsNoCase(e.desc, s_filter))
            s_order[s_orderCount++] = i;
    }
    memcpy(s_cachedFilter, s_filter, sizeof(s_cachedFilter));
    s_filterReady = true;
    s_orderDirty = true;
}

static void sortOrder()
{
    ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (!specs || specs->SpecsCount == 0) {
        if (s_hadSortSpecs)
            std::sort(s_order, s_order + s_orderCount);
        s_hadSortSpecs = false;
        s_orderDirty = false;
        return;
    }

    s_hadSortSpecs = true;
    const int col = specs->Specs[0].ColumnIndex;
    const bool asc = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    if (col != 0 && !s_orderDirty && !specs->SpecsDirty)
        return;
    std::sort(s_order, s_order + s_orderCount, [col, asc](int a, int b) {
        const EventFlagEntry& x = kEventFlags[a];
        const EventFlagEntry& y = kEventFlags[b];
        int c;
        switch (col) {
        case 0:
            c = (int)dComIfGs_isEventBit(x.byteIndex, x.mask) -
                (int)dComIfGs_isEventBit(y.byteIndex, y.mask);
            break;
        case 1:  c = strcmp(x.name, y.name);         break;
        case 2:  c = strcmp(x.location, y.location); break;
        default: c = strcmp(x.desc, y.desc);         break;
        }
        if (c == 0)
            c = a - b;
        return asc ? c < 0 : c > 0;
    });
    specs->SpecsDirty = false;
    s_orderDirty = false;
}

static void drawEventsTab()
{
    ImGui::SetNextItemWidth(-1.0f);
    Ui::Field::TextWithHint("##filter", "Search by name, area or description",
                            s_filter, sizeof(s_filter));
    refilter();
    ImGui::TextDisabled("%d of %d flags", s_orderCount, kEventFlagCount);

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoSavedSettings |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
    if (!ImGui::BeginTable("##events", 4, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    sortOrder();

    ImGuiListClipper clipper;
    clipper.Begin(s_orderCount);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const EventFlagEntry& e = kEventFlags[s_order[row]];
            ImGui::PushID(s_order[row]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = dComIfGs_isEventBit(e.byteIndex, e.mask);
            if (ImGui::Checkbox("##on", &on))
                dComIfGs_setEventBit(e.byteIndex, e.mask, on);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.location);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.desc);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

static void drawWordBitGrid(const char* label, volatile u32* words, int count)
{
    if (!ImGui::TreeNode(label))
        return;
    const int total = count * 32;
    for (int i = 0; i < total; ++i) {
        if ((i & 15) == 0)
            ImGui::Text("%3d", i);
        ImGui::SameLine();
        ImGui::PushID(i);
        bool on = dComIfGs_isBit(words, i);
        if (ImGui::Checkbox("##b", &on))
            dComIfGs_setBit(words, i, on);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

static void drawByteBitGrid(const char* label, volatile u8* bytes, int count)
{
    if (!ImGui::TreeNode(label))
        return;
    for (int b = 0; b < count; ++b) {
        ImGui::Text("%2d", b);
        for (int bit = 0; bit < 8; ++bit) {
            ImGui::SameLine();
            ImGui::PushID(b * 8 + bit);
            bool on = (bytes[b] >> bit) & 1u;
            if (ImGui::Checkbox("##e", &on)) {
                if (on)
                    bytes[b] |= (u8)(1u << bit);
                else
                    bytes[b] &= (u8)~(1u << bit);
            }
            ImGui::PopID();
        }
    }
    ImGui::TreePop();
}

static void dungeonItemCell(volatile dSv_memBit_c* m, const char* label, int bit)
{
    ImGui::TableNextColumn();
    bool on = (m->mDungeonItem >> bit) & 1u;
    if (ImGui::Checkbox(label, &on)) {
        if (on)
            m->mDungeonItem |= (u8)(1u << bit);
        else
            m->mDungeonItem &= (u8)~(1u << bit);
    }
}

static void drawDungeon(volatile dSv_memBit_c* m)
{
    ImGui::SeparatorText("Dungeon Items");
    if (ImGui::BeginTable("##dungeonitems", 2, ImGuiTableFlags_SizingStretchSame)) {
        dungeonItemCell(m, "Map", dSv_MEMBIT_MAP);
        dungeonItemCell(m, "Compass", dSv_MEMBIT_COMPASS);
        dungeonItemCell(m, "Boss Key", dSv_MEMBIT_BOSS_KEY);
        dungeonItemCell(m, "Boss Defeated", dSv_MEMBIT_BOSS_DEFEATED);
        dungeonItemCell(m, "Heart Container", dSv_MEMBIT_HEART_CONTAINER);
        dungeonItemCell(m, "Boss Cutscene Seen", dSv_MEMBIT_BOSS_DEMO);
        dungeonItemCell(m, "Ooccoo", dSv_MEMBIT_OOCCOO);
        dungeonItemCell(m, "Miniboss Defeated", dSv_MEMBIT_MINIBOSS);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Small Keys");
    int keys = m->mKeyNum;
    ImGui::SetNextItemWidth(120.0f);
    if (Ui::Field::Int("Keys", &keys)) {
        if (keys < 0)
            keys = 0;
        if (keys > 255)
            keys = 255;
        m->mKeyNum = (u8)keys;
    }
}

static bool s_followSlot = true;
static int  s_eventSlot = 0;

static void drawStageEvents()
{
    ImGui::SeparatorText("Stage Events");

    int firstActive = -1;
    int activeCount = 0;
    for (int i = 0; i < dSv_ZONE_COUNT; ++i) {
        if (dComIfGs_getZone(i)->mRoomNo != -1) {
            if (firstActive < 0)
                firstActive = i;
            ++activeCount;
        }
    }

    ImGui::Checkbox("Follow active slot", &s_followSlot);
    if (s_followSlot && firstActive >= 0)
        s_eventSlot = firstActive;
    if (!s_followSlot) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (Ui::Field::Int("Slot", &s_eventSlot)) {
            if (s_eventSlot < 0)
                s_eventSlot = 0;
            if (s_eventSlot >= dSv_ZONE_COUNT)
                s_eventSlot = dSv_ZONE_COUNT - 1;
        }
    }

    volatile dSv_zone_c* zone = dComIfGs_getZone(s_eventSlot);
    ImGui::TextDisabled("Slot %d is %s, %d of %d active", s_eventSlot,
                        zone->mRoomNo == -1 ? "empty" : "active", activeCount,
                        dSv_ZONE_COUNT);
    drawByteBitGrid("Event bits", (volatile u8*)zone, (int)sizeof(dSv_zone_c));
}

static void drawAreaTab()
{
    volatile dSv_memBit_c* m = dComIfGs_getTempBit();

    if (strncmp(dStage_getStageName(), "D_MN", 4) == 0)
        drawDungeon(m);

    ImGui::SeparatorText("Area Flags");
    drawWordBitGrid("Chests", m->mTbox, 2);
    drawWordBitGrid("Switches", m->mSwitch, 4);
    drawWordBitGrid("Items", m->mItem, 1);

    drawStageEvents();
}

struct WarpFlag {
    const char* name;
    u16         saveOffset;
    u8          bit;
    const char* stageA;
    const char* stageB;
};

static const WarpFlag kWarps[] = {
    { "Ordon Spring",       0x2FD, 4, "F_SP104", nullptr   },
    { "S. Faron Woods",     0x343, 7, "F_SP108", nullptr   },
    { "N. Faron Woods",     0x33B, 2, "F_SP108", nullptr   },
    { "Kakariko Gorge",     0x3B9, 5, "F_SP121", nullptr   },
    { "Kakariko Village",   0x358, 7, "F_SP109", nullptr   },
    { "Death Mountain",     0x359, 5, "F_SP109", "F_SP110" },
    { "Bridge of Eldin",    0x3C7, 3, "F_SP121", nullptr   },
    { "Zora's Domain",      0x37B, 2, "F_SP113", nullptr   },
    { "Lake Hylia",         0x37A, 2, "F_SP115", nullptr   },
    { "Castle Town",        0x3BB, 3, "F_SP122", nullptr   },
    { "Upper Zora's River", 0x379, 5, "F_SP126", nullptr   },
    { "Gerudo Mesa",        0x439, 5, "F_SP124", nullptr   },
    { "Mirror Chamber",     0x43E, 0, "F_SP125", nullptr   },
    { "Snowpeak Top",       0x3F9, 5, "F_SP114", nullptr   },
    { "Sacred Grove",       0x3E7, 4, "F_SP117", nullptr   },
};
static const int kWarpCount = (int)(sizeof(kWarps) / sizeof(kWarps[0]));

static bool warpInStage(const WarpFlag& w, const char* stage)
{
    if (w.stageA && strncmp(stage, w.stageA, dStage_NAME_LEN) == 0)
        return true;
    if (w.stageB && strncmp(stage, w.stageB, dStage_NAME_LEN) == 0)
        return true;
    return false;
}

static void setBit(volatile u8* p, u8 bit, bool on)
{
    if (on)
        *p |= (u8)(1u << bit);
    else
        *p &= (u8)~(1u << bit);
}

// The saved bit only applies on the next area load, so mirror it live here.
static void setWarpUnlocked(const WarpFlag& w, bool on, bool inStage)
{
    setBit(dComIfGs_saveByte(w.saveOffset), w.bit, on);
    if (!inStage)
        return;
    const u32 live = (w.saveOffset - offsetof(dSv_info_c, mSave)) % sizeof(dSv_memBit_c);
    setBit((volatile u8*)dComIfGs_getTempBit() + live, w.bit, on);
}

static void setAllWarps(bool on, const char* stage)
{
    for (int i = 0; i < kWarpCount; ++i)
        setWarpUnlocked(kWarps[i], on, warpInStage(kWarps[i], stage));
}

static void drawWarpsTab()
{
    const char* stage = dStage_getStageName();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##warps", 1, flags)) {
        ImGui::TableSetupColumn("Warp", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < kWarpCount; ++i) {
            const WarpFlag& w = kWarps[i];
            bool on = dComIfGs_getSaveBitAt(w.saveOffset, w.bit);
            const bool inStage = warpInStage(w, stage);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (inStage)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            if (ImGui::Checkbox(w.name, &on))
                setWarpUnlocked(w, on, inStage);
            if (inStage) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("This portal is in the current area.");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Unlock all"))
        setAllWarps(true, stage);
    ImGui::SameLine();
    if (ImGui::Button("Lock all"))
        setAllWarps(false, stage);
}

void DrawWindow()
{
    if (!s_open)
        return;

    ImGui::SetNextWindowPos(ImVec2(580.0f, 100.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(660.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (!dComIfGp_getPlayer()) {
            ImGui::TextDisabled("Load a save first.");
        } else if (ImGui::BeginTabBar("##world")) {
            Ui::Nav::RegisterCurrentTabBar();
            if (ImGui::BeginTabItem("Event Flags"))          { drawEventsTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Dungeon / Area Flags")) { drawAreaTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Warp Portals"))         { drawWarpsTab();  ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
}
}
