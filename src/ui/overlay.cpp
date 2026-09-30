#include "ui/overlay.h"

#include "cheats/cheats.h"
#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/rebind.h"
#include "core/settings.h"
#include "render/renderer.h"
#include "ui/init_toast.h"
#include "ui/menu.h"
#include "ui/menu_nav.h"
#include "ui/notifications.h"
#include "ui/osk.h"
#include "ui/quick_access.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"
#include "ui/watermark.h"

#include "d/d_event.h"
#include "m_Do/m_Do_graphic.h"

#include "imgui.h"

#include <coreinit/time.h>
#include <gx2/surface.h>

namespace Ui {
namespace Overlay {
static bool s_menuOpen = false;
static bool s_started = false;
static bool s_initToastShown = false;
static bool s_frozeGame = false;
static OSTime s_lastFrameTime = 0;
static float  s_frameSeconds = 0.0f;
static const float kMaxFrameSeconds = 0.25f;

bool IsMenuOpen() { return s_menuOpen; }

float FrameSeconds() { return s_frameSeconds; }

static bool isTvShowingGame()
{
    return !mDoGph_isDrcOnly();
}

static void measureFrame()
{
    const OSTime now = OSGetTime();
    float seconds = 0.0f;
    if (s_lastFrameTime != 0 && now > s_lastFrameTime)
        seconds = (float)OSTicksToMicroseconds(now - s_lastFrameTime) / 1000000.0f;
    s_lastFrameTime = now;
    s_frameSeconds = seconds > kMaxFrameSeconds ? kMaxFrameSeconds : seconds;
}

static bool overlayOwnsInput()
{
    return s_menuOpen || QuickAccess::IsPageFocused();
}

static bool s_touchSwallowed = false;

static bool blockWanted(uint32_t held, bool touchOnWidget, bool touching)
{
    const bool block = overlayOwnsInput() || Osk::IsOpen() || Watermark::IsInteracting() ||
                       touchOnWidget || Hotkeys::OverlayComboHeld(held) ||
                       (!overlayOwnsInput() && Cheats::WantsInputBlock(held));
    if (!touching)
        s_touchSwallowed = false;
    else if (block)
        s_touchSwallowed = true;
    return block || s_touchSwallowed;
}

static bool blockWantedNow()
{
    return blockWanted(Input::Current().held, false, Input::GetTouchPoint(nullptr, nullptr));
}

void SetMenuOpen(bool open)
{
    if (s_menuOpen == open)
        return;
    s_menuOpen = open;
    Logger::Log("menu %s (drawn screen %u, gamepad only %d)", open ? "open" : "closed",
                (unsigned)Config::g_settings.drawnScreen, (int)mDoGph_isDrcOnly());

    if (!open)
        Rebind::Cancel();
    if (open && !QuickAccess::OnMenuOpened())
        Menu::OnOpened();
    Input::SetBlockGameInput(blockWantedNow());
}

void OnApplicationStart()
{
    s_started = true;
    s_initToastShown = false;
    s_menuOpen = false;
    s_frozeGame = false;
    s_touchSwallowed = false;
    s_lastFrameTime = 0;
    s_frameSeconds = 0.0f;

    Input::OnApplicationStart();
    Hotkeys::OnApplicationStart();
    Rebind::OnApplicationStart();
    Menu::OnApplicationStart();
    Config::OnApplicationStart();
    Osk::OnApplicationStart();
    Notifications::Clear();
}

void OnApplicationEnd()
{
    if (s_started)
        Config::Flush();
    s_started = false;
    s_menuOpen = false;
    Renderer::ResetDeviceObjects();
    Notifications::Clear();
}

void OnPadSampled()
{
    uint32_t held = 0;
    float tx = -1.0f, ty = -1.0f;
    if (!Input::PeekLive(&held, &tx, &ty))
        return;
    const bool touching = tx >= 0.0f;
    const bool onWidget = touching && Watermark::HitTest(tx, ty);
    Input::SetBlockGameInput(blockWanted(held, onWidget, touching));
}

static bool touchReachesMenu()
{
    return Config::g_settings.drawnScreen != Config::DRAWN_SCREEN_TV || Osk::IsOpen();
}

ScreenContent ContentForScreen(bool isTv)
{
    const bool tvShowing = isTvShowingGame();

    bool full;
    switch (Config::g_settings.drawnScreen) {
    case Config::DRAWN_SCREEN_GAMEPAD: full = !isTv; break;
    case Config::DRAWN_SCREEN_TV:      full = tvShowing ? isTv : !isTv; break;
    default:                           full = isTv ? tvShowing : true; break;
    }

    if (full)
        return SCREEN_ALL;
    if (isTv)
        return Config::g_settings.drawnScreen == Config::DRAWN_SCREEN_GAMEPAD && tvShowing
                   ? SCREEN_GAME : SCREEN_NOTHING;
    return SCREEN_TOP;
}

bool HasContentFor(ScreenContent content, bool isTv)
{
    if (content == SCREEN_NOTHING)
        return false;
    if (content == SCREEN_ALL)
        return true;
    if (content == SCREEN_GAME)
        return Renderer::HasWorldContent() || Renderer::HasToastContent() ||
               (Config::g_settings.hudToTv && Renderer::HasGameScreenListContent());
    return Renderer::HasTopLayerContent() ||
           (!isTv && Renderer::HasGamePadOnlyContent());
}

static void updateFreeze()
{
    const bool want = Config::g_settings.freezeOnMenu && overlayOwnsInput();
    if (want) {
        dEvt_setEventRunning(true);
        s_frozeGame = true;
    } else if (s_frozeGame) {
        dEvt_setEventRunning(false);
        s_frozeGame = false;
    }
}

void Tick()
{
    Input::Sample();
    Input::BeginFrame();
    Config::Poll();

    if (Input::HotkeyToggled())
        SetMenuOpen(!s_menuOpen);
    if (Watermark::Tick(!Osk::IsOpen()))
        SetMenuOpen(!s_menuOpen);
    if (Input::QuickAccessToggled())
        QuickAccess::OnHotkey(s_menuOpen);

    if (!Rebind::BlocksMenuInput() && QuickAccess::HandleBack())
        Input::DrainHeld();
    Input::SetBlockGameInput(blockWantedNow());
    Hotkeys::Tick();
    Rebind::Tick();
    updateFreeze();

    const bool gameTools = !overlayOwnsInput() && !Rebind::IsActive();
    Cheats::Tick(gameTools);

    static int s_gameOnTv = -1;
    const int gameOnTv = isTvShowingGame() ? 1 : 0;
    if (gameOnTv != s_gameOnTv) {
        s_gameOnTv = gameOnTv;
        Logger::Log("game is on the %s; the world layer follows it",
                    gameOnTv ? "TV" : "GamePad");
    }
}

bool PrepareFrame(float logicalWidth, float logicalHeight)
{
    const bool ownsInput = overlayOwnsInput();

    const float uiScale =
        Config::g_settings.uiScale < 1.0f ? 1.0f : Config::g_settings.uiScale;
    const float lw = logicalWidth / uiScale;
    const float lh = logicalHeight / uiScale;
    Renderer::SetUiScale(uiScale);

    if (!Renderer::IsReady()) {
        Renderer::Init(lw, lh);
        if (!Renderer::IsReady())
            return false;
    }

    measureFrame();
    Renderer::NewFrame(lw, lh, 1.0f / 60.0f);

    const bool oskOwnsInput = Osk::ProcessInput(ImGui::GetIO());
    uint32_t feedFlags = Field::IsSteeringSlider() ? Input::FEED_NO_HORIZONTAL : 0u;
    if (!touchReachesMenu() || Watermark::IsInteracting())
        feedFlags |= Input::FEED_NO_TOUCH;
    if (Rebind::BlocksMenuInput())
        feedFlags |= Input::FEED_NO_BUTTONS;
    Input::FeedMenu(ImGui::GetIO(), lw, lh, ownsInput && !oskOwnsInput, feedFlags);

    if (!s_initToastShown) {
        s_initToastShown = true;
        if (Config::g_settings.showInitToast)
            InitToast::Arm();
    }

    if (s_menuOpen)
        Menu::Draw(ImGui::GetIO());
    Watermark::Draw();

    if (!InitToast::IsActive() || ownsInput)
        QuickAccess::DrawPageWindow(s_menuOpen);

    InitToast::Draw(ImGui::GetIO());
    Osk::Draw();

    Window::ResolveFocusRequest();
    Nav::Update(ImGui::GetIO(), Input::Current(),
                s_menuOpen && !oskOwnsInput && !Rebind::BlocksMenuInput(),
                !s_menuOpen && QuickAccess::IsPageFocused() && !oskOwnsInput);

    if (Config::g_settings.toastsEnabled)
        Notifications::Draw(ImGui::GetIO());

    Renderer::FinishFrame();
    return true;
}

static bool isGameScreen(bool isTv)
{
    return isTv == isTvShowingGame();
}

void DrawPrepared(GX2ColorBuffer* target, ScreenContent content, bool isTv)
{
    if (!target || content == SCREEN_NOTHING)
        return;
    if (content == SCREEN_TOP) {
        Renderer::DrawPreparedTopLayer(target);
    } else if (content == SCREEN_GAME) {
        Renderer::DrawGameLayers(target, Config::g_settings.hudToTv);
    } else {
        const bool gameScreen = isGameScreen(isTv);
        const ScreenContent other = ContentForScreen(!isTv);
        const bool hudElsewhere = !gameScreen && Config::g_settings.hudOnGameScreen &&
                                  (other == SCREEN_ALL ||
                                   (other == SCREEN_GAME && Config::g_settings.hudToTv));
        Renderer::DrawPrepared(target, gameScreen, !hudElsewhere);
    }
    if (!isTv)
        Renderer::DrawGamePadOnlyLayer(target);
}
}
}
