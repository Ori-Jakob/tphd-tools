#include "tools/warp.h"

#include "tools/warp_table.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "d/d_stage.h"

#include "imgui.h"

#include <string.h>

namespace Tools {
namespace Warp {
static const char kWindowName[] = "Warps";
static const int  kMaxRoom = 63;
static const int  kMaxSpawn = 255;
static const int  kMaxLayer = 15;

static bool s_open = false;
static bool s_seeded = false;
static char s_stage[dStage_NAME_LEN] = "";
static int  s_room = 0;
static int  s_spawn = 0;
static int  s_layer = dStage_LAYER_DEFAULT;

static int clampInt(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

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
    s_room = dStage_getRoomNo();
    s_spawn = dStage_getSpawn();
    s_layer = dStage_getLayer();
}

static void choose(const Destination& d)
{
    setStage(d.stage);
    s_room = d.room;
    s_spawn = d.spawn;
    s_layer = dStage_LAYER_DEFAULT;
}

static bool isChosen(const Destination& d)
{
    return strcmp(s_stage, d.stage) == 0 && s_room == d.room && s_spawn == d.spawn;
}

static void drawDestinations()
{
    const char* area = nullptr;
    bool open = false;
    for (int i = 0; i < kDestinationCount; ++i) {
        const Destination& d = kDestinations[i];
        if (!area || strcmp(area, d.area) != 0) {
            area = d.area;
            open = ImGui::CollapsingHeader(d.area);
        }
        if (!open)
            continue;
        ImGui::PushID(i);
        ImGui::Indent();
        if (ImGui::Selectable(d.name, isChosen(d)))
            choose(d);
        ImGui::Unindent();
        ImGui::PopID();
    }
}

void DrawMenuItem()
{
    Ui::Window::Checkbox("Warps", &s_open, kWindowName);
}

void DrawWindow()
{
    if (!s_open)
        return;
    if (!s_seeded) {
        useCurrent();
        s_seeded = true;
    }

    ImGui::SetNextWindowPos(ImVec2(60.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        char here[dStage_NAME_LEN + 1];
        memcpy(here, dStage_getStageName(), dStage_NAME_LEN);
        here[dStage_NAME_LEN] = '\0';
        ImGui::Text("Current: %s  room %d  layer %d", here, (int)dStage_getRoomNo(),
                    (int)dStage_getLayer());
        ImGui::Separator();

        ImGui::SetNextItemWidth(120.0f);
        Ui::Field::Text("Stage", s_stage, sizeof(s_stage));
        Ui::Field::Int("Room", &s_room);
        Ui::Field::Int("Spawn", &s_spawn);
        Ui::Field::Int("Layer", &s_layer);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("-1 uses the layer for your story progress.");
        s_room = clampInt(s_room, 0, kMaxRoom);
        s_spawn = clampInt(s_spawn, 0, kMaxSpawn);
        s_layer = clampInt(s_layer, dStage_LAYER_DEFAULT, kMaxLayer);

        if (ImGui::Button("Use Current"))
            useCurrent();
        ImGui::SameLine();
        ImGui::BeginDisabled(!s_stage[0]);
        if (ImGui::Button("Warp"))
            dStage_setNextStage(s_stage, (s8)s_room, (s16)s_spawn, (s8)s_layer);
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::BeginChild("##destinations", ImVec2(0.0f, 0.0f), true);
        drawDestinations();
        ImGui::EndChild();
    }
    ImGui::End();
}
}
}
