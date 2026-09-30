#include "tools/roll_checker.h"

#include "core/config.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"
#include "ui/ui_control.h"
#include "ui/ui_window.h"
#include "ui/window_state.h"

#include "SSystem/SComponent/c_counter.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"

#include "imgui.h"

#include <float.h>
#include <stdio.h>

namespace Tools {
namespace RollChecker {
static const char* const kWindowName = "Roll Checker";
static const char* const kControlId = "practice.roll_checker";

static const float kBaseW = 180.0f;
static const float kBaseH = 72.0f;
// The spare height leaves room for the title bar while the menu is open, so the text does not shrink.
static const float kContentH = 48.0f;
static const float kMinW = 140.0f;
static const float kMaxW = 360.0f;
static const float kDefaultX = 220.0f;
static const float kDefaultY = 90.0f;

static const u32 kNormalEndFrame   = 18;
static const u32 kNormalEarlyFrame = 12;
static const u32 kNormalLateFrame  = 23;
static const u32 kDiveEndFrame     = 13;
static const u32 kDiveEarlyFrame   = 7;
static const u32 kDiveLateFrame    = 18;

static const ImU32 kColTitle = IM_COL32(180, 190, 204, 210);
static const ImU32 kColReady = IM_COL32(180, 190, 204, 220);
static const ImU32 kColExact = IM_COL32(64, 207, 142, 255);
static const ImU32 kColEarly = IM_COL32(255, 214, 92, 255);
static const ImU32 kColLate  = IM_COL32(255, 126, 126, 255);

enum Result {
    RESULT_READY = 0,
    RESULT_EXACT,
    RESULT_EARLY,
    RESULT_LATE,
};

static Ui::WindowState s_win = { false, kDefaultX, kDefaultY, kBaseW, kBaseH };
static bool  s_applyPos = false, s_applySize = false;
static float s_layoutK = 0.0f;
static bool  s_active = false;

static Result s_result = RESULT_READY;
static u32    s_resultFrames = 0;

static u32  s_rollStartFrame = 0;
static u32  s_currentDelta = 0;
static u32  s_rollEndFrame = 0;
static u32  s_earlyCheckFrame = 0;
static u32  s_lateCheckFrame = 0;
static u16  s_previousAction = 0;
static bool s_gamePaused = false;
static bool s_suppressUntilRollEnds = false;

static void resetTiming()
{
    s_rollStartFrame = 0;
    s_currentDelta = 0;
    s_rollEndFrame = 0;
    s_earlyCheckFrame = 0;
    s_lateCheckFrame = 0;
    s_gamePaused = false;
}

static void resetAll()
{
    resetTiming();
    s_previousAction = 0;
    s_suppressUntilRollEnds = false;
    s_result = RESULT_READY;
    s_resultFrames = 0;
}

static void showResult(Result result, u32 frames)
{
    s_result = result;
    s_resultFrames = frames;
}

static bool actionPressed()
{
    return (Input::Current().pressed & (Input::BTN_A | Input::BTN_ZR)) != 0;
}

// The frame counter keeps running while the game is paused, so the roll start slides with it.
static bool compensatePause(u32 frame)
{
    if (dComIfGp_isPauseFlag()) {
        ++s_rollStartFrame;
        s_currentDelta = frame - s_rollStartFrame;
        s_gamePaused = true;
        return true;
    }

    if (s_gamePaused) {
        s_gamePaused = false;
        ++s_rollStartFrame;
    }
    return false;
}

static void applyState()
{
    if (s_win.w < kMinW) s_win.w = kMinW;
    if (s_win.w > kMaxW) s_win.w = kMaxW;
    s_win.h = s_win.w * kBaseH / kBaseW;
    s_applyPos = s_applySize = true;
}

static bool drawToggle(const Ui::Control::Descriptor* d, Ui::Control::Surface surface)
{
    const bool changed = surface == Ui::Control::SURFACE_MENU
                             ? Ui::Window::Checkbox(d->name, &s_win.enabled, kWindowName)
                             : ImGui::Checkbox(d->name, &s_win.enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rates the timing of your A press after a roll.");
    if (changed)
        Config::MarkDirty();
    return changed;
}

void RegisterControls()
{
    Ui::Control::Descriptor d = { kControlId, "Roll Checker", "Practice / Timing",
                                  drawToggle, nullptr, nullptr, 0 };
    Ui::Control::Register(d);
}

void DrawMenuItem()
{
    Ui::Control::Draw(kControlId, Ui::Control::SURFACE_MENU);
}

static void keepAspect(ImGuiSizeCallbackData* data)
{
    const float ratio = *(const float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

static void drawCentered(ImDrawList* dl, ImFont* font, ImVec2 center, float size,
                         const char* text, ImU32 color, bool bold)
{
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    Hud::DrawText(dl, font, size, ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f),
                  color, text, bold);
}

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    if (!s_win.enabled)
        return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float aspect = kBaseW / kBaseH;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    const float background = Config::g_settings.overlayOpacity;
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinW * k, kMinW * k / aspect),
                                        ImVec2(kMaxW * k, kMaxW * k / aspect),
                                        keepAspect, (void*)&aspect);
    ImGui::SetNextWindowPos(ImVec2(s_win.x * k, s_win.y * k),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_win.w * k, s_win.h * k),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = s_applySize = false;
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.5f * background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * k, style.FramePadding.y * k));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    const bool wasEnabled = s_win.enabled;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    const bool open = ImGui::Begin(kWindowName, menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / kContentH < scale) scale = avail.y / kContentH;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 origin(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                            cursor.y + (avail.y - kContentH * scale) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        dl->AddRectFilled(cursor, ImVec2(cursor.x + avail.x, cursor.y + avail.y),
                          IM_COL32(0, 0, 0, (int)(115.0f * background)), 6.0f * scale);

        char text[32];
        ImU32 color;
        switch (s_result) {
        case RESULT_EXACT:
            snprintf(text, sizeof(text), "Perfect");
            color = kColExact;
            break;
        case RESULT_EARLY:
            snprintf(text, sizeof(text), "%uf early", (unsigned)s_resultFrames);
            color = kColEarly;
            break;
        case RESULT_LATE:
            snprintf(text, sizeof(text), "%uf late", (unsigned)s_resultFrames);
            color = kColLate;
            break;
        default:
            snprintf(text, sizeof(text), "Ready");
            color = kColReady;
            break;
        }

        const bool bold = Config::g_settings.boldLetters;
        const float centerX = origin.x + kBaseW * 0.5f * scale;
        drawCentered(dl, font, ImVec2(centerX, origin.y + 10.0f * scale), 10.0f * scale,
                     "ROLL CHECKER", kColTitle, bold);
        drawCentered(dl, font, ImVec2(centerX, origin.y + 33.0f * scale), 17.0f * scale,
                     text, color, bold);
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) || moved(oldY, s_win.y) ||
                       moved(oldW, s_win.w) || moved(oldH, s_win.h)))
        Config::MarkDirty();
}

void Tick(bool acceptInput)
{
    if (s_win.enabled != s_active) {
        s_active = s_win.enabled;
        resetAll();
    }
    if (!s_active)
        return;

    daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer();
    if (!link || !acceptInput || dEvt_isEventRunning() ||
        dComIfGs_getStatusA()->mTransformStatus != 0) {
        resetTiming();
        s_previousAction = link ? daAlink_getProcID(link) : 0;
        s_suppressUntilRollEnds = s_previousAction == daAlink_PROC_FRONT_ROLL;
        return;
    }

    const u16 action = daAlink_getProcID(link);
    const u32 frame = cCt_getFrameCount();

    if (s_suppressUntilRollEnds) {
        if (action == daAlink_PROC_FRONT_ROLL) {
            resetTiming();
            return;
        }
        s_suppressUntilRollEnds = false;
        s_previousAction = action;
    }

    if (action == daAlink_PROC_FRONT_ROLL) {
        if (s_previousAction == daAlink_PROC_DIVE_JUMP) {
            s_rollEndFrame = kDiveEndFrame;
            s_earlyCheckFrame = kDiveEarlyFrame;
            s_lateCheckFrame = kDiveLateFrame;
        } else {
            s_rollEndFrame = kNormalEndFrame;
            s_earlyCheckFrame = kNormalEarlyFrame;
            s_lateCheckFrame = kNormalLateFrame;
        }

        if (s_currentDelta == s_rollEndFrame) {
            if (compensatePause(frame))
                return;
            if (actionPressed()) {
                showResult(RESULT_EXACT, 0);
                s_currentDelta = 0;
            } else {
                s_currentDelta = frame - s_rollStartFrame;
            }
        } else if (s_currentDelta > s_earlyCheckFrame && s_currentDelta < s_rollEndFrame) {
            if (compensatePause(frame))
                return;
            if (actionPressed())
                showResult(RESULT_EARLY, s_rollEndFrame - s_currentDelta);
            s_currentDelta = frame - s_rollStartFrame;
        } else if (s_currentDelta > 0 && s_currentDelta <= s_earlyCheckFrame) {
            if (compensatePause(frame))
                return;
            s_currentDelta = frame - s_rollStartFrame;
        } else {
            s_rollStartFrame = frame;
            s_currentDelta = 1;
            s_gamePaused = false;
        }
        return;
    }

    if (s_currentDelta > s_lateCheckFrame || s_currentDelta <= s_rollEndFrame) {
        resetTiming();
    } else if (!compensatePause(frame)) {
        if (actionPressed()) {
            showResult(RESULT_LATE, s_currentDelta - s_rollEndFrame);
            resetTiming();
        } else {
            s_currentDelta = frame - s_rollStartFrame;
        }
    }

    s_previousAction = action;
}

void OnApplicationStart()
{
    resetAll();
    s_active = s_win.enabled;
    s_layoutK = 0.0f;
    s_applyPos = s_applySize = true;
}

void ResetToDefaults()
{
    s_win.enabled = false;
    s_win.x = kDefaultX;
    s_win.y = kDefaultY;
    s_win.w = kBaseW;
    applyState();
}

void LoadConfig(const cJSON* root)
{
    Config::LoadWindow(root, "rollChecker", s_win, kMinW, kMaxW);
    applyState();
}

void SaveConfig(cJSON* root)
{
    Config::SaveWindow(root, "rollChecker", s_win);
}
}
}
