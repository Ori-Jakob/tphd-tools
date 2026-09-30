#include "tools/position_editor.h"

#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor.h"

#include "imgui.h"

namespace Tools {
namespace PositionEditor {
static const char kWindowName[] = "Position Editor";
static const int  kMaxFacing = 0xFFFF;

static bool  s_open = false;
static bool  s_seeded = false;
static float s_x = 0.0f;
static float s_y = 0.0f;
static float s_z = 0.0f;
static int   s_facing = 0;

static void useCurrent()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    s_x = link->current.pos.x;
    s_y = link->current.pos.y;
    s_z = link->current.pos.z;
    s_facing = (int)(u16)link->shape_angle.y;
    s_seeded = true;
}

static void apply()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;

    if (link->current.pos.x != s_x || link->current.pos.y != s_y ||
        link->current.pos.z != s_z) {
        cXyz pos;
        pos.set(s_x, s_y, s_z);
        link->old.pos = pos;
        link->current.pos = pos;
        link->speed.zero();
        link->speedF = 0.0f;
    }
    link->shape_angle.y = (s16)(u16)s_facing;
}

void DrawMenuItem()
{
    if (Ui::Window::Checkbox("Link Position Editor", &s_open, kWindowName) && s_open)
        s_seeded = false;
}

void DrawWindow()
{
    if (!s_open)
        return;
    if (!s_seeded)
        useCurrent();

    ImGui::SetNextWindowPos(ImVec2(420.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(250.0f);
        Ui::Field::Float("X", &s_x, "%.7f");
        ImGui::SetNextItemWidth(250.0f);
        Ui::Field::Float("Y", &s_y, "%.7f");
        ImGui::SetNextItemWidth(250.0f);
        Ui::Field::Float("Z", &s_z, "%.7f");
        ImGui::SetNextItemWidth(180.0f);
        Ui::Field::Int("Facing", &s_facing, 0, 0);
        if (s_facing < 0)
            s_facing = 0;
        if (s_facing > kMaxFacing)
            s_facing = kMaxFacing;

        ImGui::BeginDisabled(dComIfGp_getPlayer() == nullptr);
        if (ImGui::Button("Use Current"))
            useCurrent();
        ImGui::SameLine();
        if (ImGui::Button("Set"))
            apply();
        ImGui::EndDisabled();
    }
    ImGui::End();
}
}
}
