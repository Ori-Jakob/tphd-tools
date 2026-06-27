// renderer.cpp -- ImGui + native GX2 render path.
//
// The game owns GX2; we never call WHBGfxInit / GX2Init. We init ImGui + the GX2
// backend once, then each frame render into the selected game color buffer(s).
// ImGui builds one frame in TV coordinates; Draw adjusts FramebufferScale when
// the same draw data is sent to the smaller GamePad buffer. We don't restore
// render state afterwards: the only thing between us and end-of-frame is the
// copy-to-scan + swap (neither reads render registers), and the game re-binds
// its own state at the start of the next frame.
#include "renderer.h"

#include <coreinit/debug.h>     // OSReport
#include <gx2/surface.h>        // GX2ColorBuffer, GX2SetColorBuffer, GX2_RENDER_TARGET_0
#include <gx2/registers.h>      // GX2SetViewport, GX2SetScissor

#include "imgui.h"
#include "imgui_impl_gx2.h"

namespace Renderer {

static bool s_ready = false;

bool IsReady()
{
    return s_ready;
}

void Init(GX2ColorBuffer* display)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // Keyboard nav is enabled so we can synthesize the Alt menu-layer toggle that
    // lets a controller enter the top menu bar (see menu.cpp). We never feed real
    // keyboard input.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = nullptr;   // never touch the filesystem from inside the game
    io.LogFilename = nullptr;
    io.DisplaySize =
        ImVec2((float)display->surface.width, (float)display->surface.height);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(2.0f);   // Wii U output is high-res
    io.FontGlobalScale = 2.0f;

    ImGui_ImplGX2_Init();

    s_ready = true;
    OSReport("[tphd_tools] ImGui initialized (%ux%u) -- overlay live\n",
             display->surface.width, display->surface.height);
}

void NewFrame(GX2ColorBuffer* display, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize =
        ImVec2((float)display->surface.width, (float)display->surface.height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime   = deltaTime;

    ImGui_ImplGX2_NewFrame();
    ImGui::NewFrame();
}

void FinishFrame()
{
    ImGui::Render();
}

void Draw(GX2ColorBuffer* target)
{
    if (!target)
        return;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
        return;

    // The GX2 backend derives its viewport and clip rectangles from
    // FramebufferScale. Temporarily scale the TV-sized draw data to this target.
    ImVec2 oldScale = drawData->FramebufferScale;
    drawData->FramebufferScale =
        ImVec2((float)target->surface.width / drawData->DisplaySize.x,
               (float)target->surface.height / drawData->DisplaySize.y);

    GX2SetColorBuffer(target, GX2_RENDER_TARGET_0);
    GX2SetViewport(0.0f, 0.0f, (float)target->surface.width,
                   (float)target->surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, target->surface.width, target->surface.height);
    ImGui_ImplGX2_RenderDrawData(drawData);

    drawData->FramebufferScale = oldScale;
}

} // namespace Renderer
