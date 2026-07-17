// overlay.cpp -- the shared per-frame overlay body (Overlay::Present).
//
// This is the platform-agnostic heart of the overlay: it owns the once-per-frame
// loop (input consume -> hotkey -> tools/cheats -> build ImGui), then draws that
// frame to the configured TV/GamePad color buffer(s). Cemu supplies both buffers
// together. Aroma supplies the TV buffer for the frame tick and its DRC copy hook
// calls PresentGamePad separately with the same completed draw data.
//
// The ov:: shared state (settings + menu visibility) is also defined here so it
// lives in a single TU shared by both builds (it used to live in main.cpp).

#include <stdint.h>
#include <gx2/surface.h>        // GX2ColorBuffer

#include "imgui.h"
#include "overlay.h"
#include "renderer.h"
#include "input.h"
#include "logger.h"
#include "menu.h"
#include "config.h"
#include "swkbd.h"
#include "tools/save_state.h"
#include "tools/flycam.h"
#include "tools/modern_camera.h"
#include "tools/auto_splitter.h"
#include "cheats/cheats.h"
#include "debug_save.h"
#include "game/game.h"          // dCam_setFreeze (freeze-on-menu)

// Definitions of the shared state declared in overlay.h.
namespace ov {
Settings g_settings;
bool     g_menuVisible = false;
}

namespace Overlay {

// TPHD presents at 30Hz; our present hook fires once per presented frame.
static const float kDeltaTime = 1.0f / 30.0f;
static bool s_frameReady = false;

static bool drawsTv()
{
    return ov::g_settings.renderTarget == ov::RENDER_TV ||
           ov::g_settings.renderTarget == ov::RENDER_BOTH;
}

static bool drawsGamePad()
{
    return ov::g_settings.renderTarget == ov::RENDER_GAMEPAD ||
           ov::g_settings.renderTarget == ov::RENDER_BOTH;
}

static void drawTv(GX2ColorBuffer* tv)
{
    if (!tv || !drawsTv())
        return;
    Renderer::Draw(tv);
    SwKbd::DrawTV();
}

static void drawGamePad(GX2ColorBuffer* gamePad)
{
    if (!gamePad || !drawsGamePad())
        return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Logger::Log("[tphd_tools] GamePad overlay target active (%ux%u)",
                    (unsigned)gamePad->surface.width,
                    (unsigned)gamePad->surface.height);
    }
    Renderer::Draw(gamePad);
    SwKbd::DrawGamePad();
}

void Present(GX2ColorBuffer* tv, GX2ColorBuffer* gamePad)
{
    if (!tv)
        return;

    if (!Renderer::IsReady()) {
        Renderer::Init(tv);
        Logger::Log("[tphd_tools] overlay initialized (%ux%u)",
                    (unsigned)tv->surface.width, (unsigned)tv->surface.height);
        Menu::OnLoaded();   // kick off the "tools loaded" toast
    }

    // Load saved settings (retries internally until the save mount is ready) so a
    // customized hotkey is in effect before the input is consumed below.
    Config::Load();
    Tools::SaveState::Initialize();
    Tools::AutoSplitter::Initialize();

    // Consume this frame's stashed controller input, then act on the hotkey.
    Input::BeginFrame();
    if (Input::GameResetHotkeyFired())
        dComIfG_requestGameReset();
    if (Input::HotkeyToggled())
        Menu::Toggle();

    ImGuiIO& io = ImGui::GetIO();

    // System keyboard: when the menu is open, let nn::swkbd claim input for any
    // focused text field. While it's up it owns the controller, so suppress menu
    // nav for this frame.
    bool kbdActive = false;
    if (ov::g_menuVisible) {
        SwKbd::Init();
        kbdActive = SwKbd::ProcessInput(io);
    }
    if (!kbdActive)
        Input::FeedMenu(io, (float)tv->surface.width, (float)tv->surface.height);

    // Fly cam runs every frame from the live game controller (independent of the
    // menu); apply any pending save-state load / Link-position override. The
    // modern camera ticks first so it can yield the same frame the fly cam
    // activates (it checks FlyCam::IsActive()).
    Tools::ModernCamera::Tick();
    Tools::FlyCam::Tick();
    Debug::DebugSave::Tick();   // may arm an in-place reload consumed below
    Tools::SaveState::Tick();
    Tools::AutoSplitter::Tick();
    Cheats::Tick();

    // Optionally halt the game while the menu is open (the same freeze bit FlyCam
    // uses). We only release it ourselves; if FlyCam is flying it owns the bit
    // (and re-asserts it every frame), so we never clear it out from under it.
    {
        static bool froze = false;
        bool want = ov::g_settings.freezeOnMenu && ov::g_menuVisible;
        if (want) {
            dCam_setFreeze(true);
            froze = true;
        } else if (froze) {
            if (!Tools::FlyCam::IsActive())
                dCam_setFreeze(false);
            froze = false;
        }
    }

    Renderer::NewFrame(tv, kDeltaTime);
    Menu::Draw(io);
    Renderer::FinishFrame();
    s_frameReady = true;

    drawTv(tv);
    drawGamePad(gamePad);

    // Persist any setting the user just changed.
    Config::Update();
}

void PresentGamePad(GX2ColorBuffer* gamePad)
{
    if (Renderer::IsReady() && s_frameReady)
        drawGamePad(gamePad);
}

bool WantsGamePadDraw()
{
    return Renderer::IsReady() && s_frameReady && drawsGamePad();
}

void OnApplicationStart()
{
    s_frameReady = false;
    Renderer::ResetDeviceObjects();
}

void OnApplicationEnd()
{
    s_frameReady = false;
    Renderer::ResetDeviceObjects();
}

} // namespace Overlay
