// tools/warp.cpp -- see warp.h.
//
// Pick a destination from the curated area/map table (game/warp_table.h, derived
// from dusklight's gameRegions) -- clicking a map fills the stage/room/spawn/layer
// fields -- or type them by hand, then "Warp". The warp itself is just the
// mNextStage field writes in game/d_stage.h.
#include "tools/warp.h"

#include "imgui.h"
#include "game/game.h"        // dStage_* helpers
#include "game/warp_table.h"  // kWarpTable / kWarpTableCount

#include <string.h>

namespace Tools {
namespace Warp {

static bool s_enabled = false;
static char s_stage[8] = "";          // stage name to warp to (<=7 chars)
static int  s_room  = 0;
static int  s_spawn = 0;
static int  s_layer = DSTAGE_LAYER_DEFAULT;   // -1 = default/auto
static bool s_seeded = false;         // prefill from current stage on first open

void DrawMenuItem()        { ImGui::Checkbox("Warps", &s_enabled); }
bool IsEnabled()           { return s_enabled; }
void SetEnabled(bool e)    { s_enabled = e; }

static void setStage(const char* name)
{
    int i = 0;
    for (; i < (int)sizeof(s_stage) - 1 && name[i]; ++i)
        s_stage[i] = name[i];
    s_stage[i] = '\0';
}

static void useCurrent()
{
    setStage(dStage_getStageName());
    s_room  = dStage_getRoomNo();
    s_layer = dStage_getLayer();
    s_spawn = dStage_getSpawn();
}

// Fill the warp fields from a curated table entry.
static void populate(const WarpEntry& e)
{
    setStage(e.stage);
    s_room  = e.room;
    s_spawn = e.spawn;
    s_layer = DSTAGE_LAYER_DEFAULT;
}

static void drawDestinationList()
{
    // Group consecutive rows by area into collapsing headers; each map is a
    // selectable that populates the fields above.
    const char* curArea = nullptr;
    bool open = false;
    for (int i = 0; i < kWarpTableCount; ++i) {
        const WarpEntry& e = kWarpTable[i];
        if (curArea == nullptr || strcmp(curArea, e.area) != 0) {
            curArea = e.area;
            open = ImGui::CollapsingHeader(e.area);
        }
        if (!open)
            continue;
        ImGui::PushID(i);
        ImGui::Indent();
        if (ImGui::Selectable(e.name))
            populate(e);
        ImGui::Unindent();
        ImGui::PopID();
    }
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;
    if (!s_seeded) {              // seed the fields from the live stage once
        useCurrent();
        s_seeded = true;
    }

    // Resizeable; no NoSavedSettings so ImGui tracks pos/size (persisted via the
    // config file's imgui-ini blob, see config.cpp).
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(60.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Warps", &s_enabled, flags)) {
        ImGui::Text("Current: %s  room %d  layer %d",
                    dStage_getStageName(), (int)dStage_getRoomNo(), (int)dStage_getLayer());
        ImGui::Separator();

        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("Stage", s_stage, sizeof(s_stage));
        ImGui::InputInt("Room", &s_room);
        ImGui::InputInt("Spawn", &s_spawn);
        ImGui::InputInt("Layer", &s_layer);
        ImGui::SameLine();
        ImGui::TextDisabled("(-1 = default)");

        // Clamp to the engine's valid ranges.
        s_room  = s_room  < 0  ? 0  : (s_room  > 63  ? 63  : s_room);
        s_spawn = s_spawn < 0  ? 0  : (s_spawn > 255 ? 255 : s_spawn);
        s_layer = s_layer < -1 ? -1 : (s_layer > 15  ? 15  : s_layer);

        if (ImGui::Button("Use Current"))
            useCurrent();
        ImGui::SameLine();
        if (ImGui::Button("Warp") && s_stage[0])
            dStage_setNextStage(s_stage, (s8)s_room, (s16)s_spawn, (s8)s_layer);

        ImGui::Separator();
        ImGui::TextDisabled("Destinations (click to fill)");
        ImGui::BeginChild("##dests", ImVec2(0, 0), true);
        drawDestinationList();
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace Warp
} // namespace Tools
