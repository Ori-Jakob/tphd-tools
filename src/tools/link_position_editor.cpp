// tools/link_position_editor.cpp -- see tools/link_position_editor.h.
#include "tools/link_position_editor.h"

#include "imgui.h"
#include "game/game.h"

namespace Tools {
namespace LinkPositionEditor {

static bool  s_enabled = false;
static bool  s_seeded = false;
static float s_x = 0.0f;
static float s_y = 0.0f;
static float s_z = 0.0f;
static int   s_facing = 0;

void DrawMenuItem()
{
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Link Position Editor", &enabled)) {
        s_enabled = enabled;
        if (s_enabled)
            s_seeded = false;
    }
}

bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e)
{
    if (e && !s_enabled)
        s_seeded = false;
    s_enabled = e;
}

static bool populate()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return false;

    s_x = link->current.pos.x;
    s_y = link->current.pos.y;
    s_z = link->current.pos.z;
    s_facing = (int)(u16)link->shape_angle.y;
    s_seeded = true;
    return true;
}

static bool setPosition()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return false;

    bool positionChanged = link->current.pos.x != s_x ||
                           link->current.pos.y != s_y ||
                           link->current.pos.z != s_z;
    s16 facing = (s16)(u16)s_facing;
    bool facingChanged = link->shape_angle.y != facing;

    if (positionChanged) {
        cXyz pos = { s_x, s_y, s_z };
        // Keep the actor's previous transform in sync so the first frame after
        // teleporting does not see the whole displacement as actor movement.
        link->old.pos = pos;
        link->current.pos = pos;
        link->speed.x = link->speed.y = link->speed.z = 0.0f;
        link->speedF = 0.0f;
    }
    if (facingChanged)
        link->shape_angle.y = facing;

    return positionChanged || facingChanged;
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;

    if (!s_seeded)
        populate();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(420.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Position Editor", &s_enabled, flags)) {

        ImGui::SetNextItemWidth(250.0f);
        ImGui::InputFloat("X", &s_x, 0.0f, 0.0f, "%.7f");
        ImGui::SetNextItemWidth(250.0f);
        ImGui::InputFloat("Y", &s_y, 0.0f, 0.0f, "%.7f");
        ImGui::SetNextItemWidth(250.0f);
        ImGui::InputFloat("Z", &s_z, 0.0f, 0.0f, "%.7f");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputInt("Facing", &s_facing, 0, 0);
        if (s_facing < 0)
            s_facing = 0;
        if (s_facing > 0xFFFF)
            s_facing = 0xFFFF;

        if (ImGui::Button("Populate"))
            populate();
        ImGui::SameLine();
        if (ImGui::Button("Set"))
            setPosition();
    }
    ImGui::End();
}

} // namespace LinkPositionEditor
} // namespace Tools
