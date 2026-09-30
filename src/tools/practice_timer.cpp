#include "tools/practice_timer.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"
#include "ui/ui_control.h"
#include "ui/ui_hotkey.h"
#include "ui/ui_window.h"
#include "ui/window_state.h"

#include "SSystem/SComponent/c_counter.h"
#include "f_op/f_op_scene_req.h"

#include "imgui.h"

#include <coreinit/time.h>
#include <float.h>
#include <stdio.h>

namespace Tools {
namespace PracticeTimer {
static const char* const kWindowName = "Practice Timer";
static const char* const kControlId = "practice.timer";

static const float kBaseW = 196.0f;
static const float kFirstRowY = 12.0f;
static const float kRowStep = 14.0f;
static const float kBasePadding = 22.0f;
static const float kMinBaseH = 48.0f;
static const float kMinW = 150.0f;
static const float kMaxW = 380.0f;
static const float kLabelX = 12.0f;
static const float kValueRightX = 184.0f;
static const float kFontSize = 10.0f;
static const float kDefaultX = 410.0f;
static const float kDefaultY = 90.0f;

static const ImU32 kColLabel   = IM_COL32(180, 190, 204, 210);
static const ImU32 kColIdle    = IM_COL32(255, 255, 255, 255);
static const ImU32 kColRunning = IM_COL32(64, 207, 142, 255);
static const ImU32 kColHeld    = IM_COL32(255, 214, 92, 255);

static Ui::WindowState s_win = { false, kDefaultX, kDefaultY, kBaseW, kMinBaseH };
static bool  s_applyPos = false, s_applySize = false;
static float s_layoutK = 0.0f;

static bool s_frameEnabled = false;
static bool s_loadEnabled = false;
static bool s_igtEnabled = false;

static bool     s_clockRunning = false;
static bool     s_frameSegment = false;
static OSTime   s_frameRunStart = 0;
static u32      s_frameRunStartCount = 0;
static uint64_t s_frameElapsedTicks = 0;
static u32      s_frameElapsedFrames = 0;

static bool     s_loadActive = false;
static OSTime   s_loadStart = 0;
static uint64_t s_loadTotalTicks = 0;

static bool     s_igtSegment = false;
static bool     s_igtLoadHold = false;
static OSTime   s_igtRunStart = 0;
static u32      s_igtRunStartCount = 0;
static uint64_t s_igtElapsedTicks = 0;
static u32      s_igtElapsedFrames = 0;

static bool anyEnabled()   { return s_frameEnabled || s_loadEnabled || s_igtEnabled; }
static bool clockEnabled() { return s_frameEnabled || s_igtEnabled; }

static float baseHeight()
{
    unsigned rows = (unsigned)s_frameEnabled + (unsigned)s_loadEnabled + (unsigned)s_igtEnabled;
    if (rows == 0)
        rows = 1;
    const float h = kBasePadding + kRowStep * (float)rows;
    return h < kMinBaseH ? kMinBaseH : h;
}

static void applyState()
{
    if (s_win.w < kMinW) s_win.w = kMinW;
    if (s_win.w > kMaxW) s_win.w = kMaxW;
    s_win.h = s_win.w * baseHeight() / kBaseW;
    s_win.enabled = anyEnabled();
    s_applyPos = s_applySize = true;
}

static uint64_t ticksBetween(OSTime start, OSTime now)
{
    return now < start ? 0 : (uint64_t)(now - start);
}

static void commitFrameSegment(OSTime now, u32 frame)
{
    if (!s_frameSegment)
        return;
    s_frameElapsedTicks += ticksBetween(s_frameRunStart, now);
    s_frameElapsedFrames += frame - s_frameRunStartCount;
    s_frameSegment = false;
}

static void beginFrameSegment(OSTime now, u32 frame)
{
    s_frameRunStart = now;
    s_frameRunStartCount = frame;
    s_frameSegment = true;
}

static void commitIgtSegment(OSTime now, u32 frame)
{
    if (!s_igtSegment)
        return;
    s_igtElapsedTicks += ticksBetween(s_igtRunStart, now);
    s_igtElapsedFrames += frame - s_igtRunStartCount;
    s_igtSegment = false;
}

static void beginIgtSegment(OSTime now, u32 frame)
{
    s_igtRunStart = now;
    s_igtRunStartCount = frame;
    s_igtSegment = true;
}

static void resetFrame()
{
    s_frameSegment = false;
    s_frameRunStart = 0;
    s_frameRunStartCount = 0;
    s_frameElapsedTicks = 0;
    s_frameElapsedFrames = 0;
}

static void resetLoad()
{
    s_loadActive = false;
    s_loadStart = 0;
    s_loadTotalTicks = 0;
}

static void resetIgt()
{
    s_igtSegment = false;
    s_igtLoadHold = false;
    s_igtRunStart = 0;
    s_igtRunStartCount = 0;
    s_igtElapsedTicks = 0;
    s_igtElapsedFrames = 0;
}

static void clearAll()
{
    s_clockRunning = false;
    resetFrame();
    resetLoad();
    resetIgt();
}

static void resetEnabled()
{
    if (s_frameEnabled)
        resetFrame();
    if (s_loadEnabled)
        resetLoad();
    if (s_igtEnabled)
        resetIgt();
    s_clockRunning = false;
}

static void pauseClock(OSTime now, u32 frame)
{
    if (!s_clockRunning)
        return;
    if (s_frameEnabled)
        commitFrameSegment(now, frame);
    if (s_igtEnabled && !s_igtLoadHold)
        commitIgtSegment(now, frame);
    s_clockRunning = false;
    s_igtLoadHold = false;
}

static void toggleClock(OSTime now, u32 frame, bool loading)
{
    if (s_clockRunning) {
        pauseClock(now, frame);
        return;
    }

    s_clockRunning = true;
    if (s_frameEnabled)
        beginFrameSegment(now, frame);
    if (s_igtEnabled) {
        if (loading)
            s_igtLoadHold = true;
        else
            beginIgtSegment(now, frame);
    }
}

static void updateLoad(OSTime now, bool loading)
{
    if (!s_loadEnabled)
        return;

    if (loading) {
        if (!s_loadActive || now < s_loadStart) {
            s_loadActive = true;
            s_loadStart = now;
        }
    } else if (s_loadActive) {
        s_loadTotalTicks += ticksBetween(s_loadStart, now);
        s_loadActive = false;
        s_loadStart = 0;
    }
}

static void updateIgt(OSTime now, u32 frame, bool loading)
{
    if (!s_igtEnabled || !s_clockRunning)
        return;

    if (loading) {
        if (!s_igtLoadHold) {
            commitIgtSegment(now, frame);
            s_igtLoadHold = true;
        }
        return;
    }

    if (s_igtLoadHold || !s_igtSegment) {
        s_igtLoadHold = false;
        beginIgtSegment(now, frame);
    }
}

static void setFrameEnabled(bool enabled)
{
    if (s_frameEnabled == enabled)
        return;
    const OSTime now = OSGetTime();
    const u32 frame = cCt_getFrameCount();
    if (!enabled)
        commitFrameSegment(now, frame);
    else if (s_clockRunning)
        beginFrameSegment(now, frame);
    s_frameEnabled = enabled;
    if (!clockEnabled())
        pauseClock(now, frame);
    applyState();
}

static void setLoadEnabled(bool enabled)
{
    if (s_loadEnabled == enabled)
        return;
    if (!enabled && s_loadActive) {
        s_loadTotalTicks += ticksBetween(s_loadStart, OSGetTime());
        s_loadActive = false;
        s_loadStart = 0;
    }
    s_loadEnabled = enabled;
    applyState();
}

static void setIgtEnabled(bool enabled)
{
    if (s_igtEnabled == enabled)
        return;
    const OSTime now = OSGetTime();
    const u32 frame = cCt_getFrameCount();
    if (!enabled) {
        if (!s_igtLoadHold)
            commitIgtSegment(now, frame);
        s_igtLoadHold = false;
    } else if (s_clockRunning) {
        if (fopScnRq_isLoading())
            s_igtLoadHold = true;
        else
            beginIgtSegment(now, frame);
    }
    s_igtEnabled = enabled;
    if (!clockEnabled())
        pauseClock(now, frame);
    applyState();
}

static uint64_t currentFrameTicks(OSTime now)
{
    return s_frameElapsedTicks + (s_frameSegment ? ticksBetween(s_frameRunStart, now) : 0);
}

static u32 currentFrameCount(u32 frame)
{
    return s_frameElapsedFrames + (s_frameSegment ? frame - s_frameRunStartCount : 0);
}

static uint64_t currentLoadTicks(OSTime now)
{
    return s_loadTotalTicks + (s_loadActive ? ticksBetween(s_loadStart, now) : 0);
}

static uint64_t currentIgtTicks(OSTime now)
{
    return s_igtElapsedTicks + (s_igtSegment ? ticksBetween(s_igtRunStart, now) : 0);
}

static u32 currentIgtFrames(u32 frame)
{
    return s_igtElapsedFrames + (s_igtSegment ? frame - s_igtRunStartCount : 0);
}

static void formatTime(uint64_t ticks, char* buf, size_t size)
{
    const uint64_t perSecond = (uint64_t)OSTimerClockSpeed;
    const uint64_t seconds = ticks / perSecond;
    const unsigned ms = (unsigned)(((ticks % perSecond) * 1000u) / perSecond);
    const unsigned long long hours = (unsigned long long)(seconds / 3600u);
    const unsigned minutes = (unsigned)((seconds / 60u) % 60u);
    const unsigned secs = (unsigned)(seconds % 60u);
    snprintf(buf, size, hours < 100u ? "%02llu:%02u:%02u.%03u" : "%llu:%02u:%02u.%03u",
             hours, minutes, secs, ms);
}

static void formatTimeAndFrames(uint64_t ticks, u32 frames, char* buf, size_t size)
{
    char time[40];
    formatTime(ticks, time, sizeof(time));
    snprintf(buf, size, "%s  %uf", time, (unsigned)frames);
}

static ImU32 statusColor(bool running, bool loading, bool hasElapsed)
{
    if (loading)
        return kColHeld;
    if (running)
        return kColRunning;
    return hasElapsed ? kColHeld : kColIdle;
}

static void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

static bool drawTimers(const Ui::Control::Descriptor*, Ui::Control::Surface surface)
{
    const bool wasEnabled = anyEnabled();
    bool frame = s_frameEnabled, load = s_loadEnabled, igt = s_igtEnabled;
    bool changed = false;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Timer:");
    ImGui::SameLine();
    if (ImGui::Checkbox("Frame", &frame)) {
        setFrameEnabled(frame);
        changed = true;
    }
    tooltip("Real time and frames while running.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Load", &load)) {
        setLoadEnabled(load);
        changed = true;
    }
    tooltip("Total time spent loading.");
    ImGui::SameLine();
    if (ImGui::Checkbox("IGT", &igt)) {
        setIgtEnabled(igt);
        changed = true;
    }
    tooltip("Real time without loads.");

    if (changed)
        Config::MarkDirty();
    if (surface == Ui::Control::SURFACE_MENU && !wasEnabled && anyEnabled()) {
        ImGui::CloseCurrentPopup();
        Ui::Window::RequestFocus(kWindowName);
    }
    return changed;
}

static bool drawTimerOptions(const Ui::Control::Descriptor*, Ui::Control::Surface)
{
    if (!anyEnabled())
        return false;
    ImGui::Indent();
    if (clockEnabled())
        Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_TIMER_TOGGLE), nullptr, ": start/stop",
                             true);
    if (ImGui::Button("Reset"))
        resetEnabled();
    ImGui::SameLine(0.0f, 0.0f);
    Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_TIMER_RESET), "  ", nullptr, true);
    ImGui::Unindent();
    return false;
}

void RegisterControls()
{
    Ui::Control::Descriptor d = { kControlId, "Practice Timer", "Practice / Timing",
                                  drawTimers, drawTimerOptions, nullptr, 0 };
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

struct RowCtx {
    ImDrawList* dl;
    ImFont*     font;
    ImVec2      origin;
    float       scale;
    bool        bold;
};

static void drawRow(const RowCtx& ctx, float y, const char* label, const char* value,
                    ImU32 valueColor)
{
    const float size = kFontSize * ctx.scale;
    const ImVec2 lp(ctx.origin.x + kLabelX * ctx.scale, ctx.origin.y + y * ctx.scale);
    Hud::DrawText(ctx.dl, ctx.font, size, lp, kColLabel, label, ctx.bold);
    const float w = ctx.font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x;
    Hud::DrawText(ctx.dl, ctx.font, size,
                  ImVec2(ctx.origin.x + kValueRightX * ctx.scale - w, lp.y), valueColor, value,
                  ctx.bold);
}

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    if (!anyEnabled())
        return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float baseH = baseHeight();
    const float aspect = kBaseW / baseH;
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

    bool keepOpen = true;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    const bool open = ImGui::Begin(kWindowName, menuActive ? &keepOpen : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        const OSTime now = OSGetTime();
        const u32 frame = cCt_getFrameCount();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / baseH < scale) scale = avail.y / baseH;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        RowCtx ctx;
        ctx.dl = ImGui::GetWindowDrawList();
        ctx.font = ImGui::GetFont();
        ctx.scale = scale;
        ctx.bold = Config::g_settings.boldLetters;
        ctx.origin = ImVec2(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                            cursor.y + (avail.y - baseH * scale) * 0.5f);
        ctx.dl->AddRectFilled(ctx.origin,
                              ImVec2(ctx.origin.x + kBaseW * scale, ctx.origin.y + baseH * scale),
                              IM_COL32(0, 0, 0, (int)(115.0f * background)), 6.0f * scale);

        char value[48];
        float rowY = kFirstRowY;
        if (s_frameEnabled) {
            formatTimeAndFrames(currentFrameTicks(now), currentFrameCount(frame), value,
                                sizeof(value));
            drawRow(ctx, rowY, "Frame:", value,
                    statusColor(s_clockRunning, false,
                                s_frameElapsedTicks != 0 || s_frameElapsedFrames != 0));
            rowY += kRowStep;
        }
        if (s_loadEnabled) {
            formatTime(currentLoadTicks(now), value, sizeof(value));
            drawRow(ctx, rowY, "Load:", value,
                    statusColor(false, s_loadActive, s_loadTotalTicks != 0));
            rowY += kRowStep;
        }
        if (s_igtEnabled) {
            formatTimeAndFrames(currentIgtTicks(now), currentIgtFrames(frame), value,
                                sizeof(value));
            drawRow(ctx, rowY, "IGT:", value,
                    statusColor(s_clockRunning && !s_igtLoadHold, s_igtLoadHold,
                                s_igtElapsedTicks != 0 || s_igtElapsedFrames != 0));
        }
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    if (!keepOpen) {
        setFrameEnabled(false);
        setLoadEnabled(false);
        setIgtEnabled(false);
        Config::MarkDirty();
    } else if (menuActive && (moved(oldX, s_win.x) || moved(oldY, s_win.y) ||
                              moved(oldW, s_win.w) || moved(oldH, s_win.h))) {
        Config::MarkDirty();
    }
}

void Tick(bool acceptInput)
{
    if (!anyEnabled())
        return;

    const OSTime now = OSGetTime();
    const u32 frame = cCt_getFrameCount();
    const bool loading = fopScnRq_isLoading();

    if (s_frameEnabled && s_clockRunning && !s_frameSegment)
        beginFrameSegment(now, frame);
    updateLoad(now, loading);
    updateIgt(now, frame, loading);

    if (!acceptInput)
        return;
    if (clockEnabled() && Hotkeys::Pressed(Hotkeys::HOTKEY_TIMER_TOGGLE)) {
        toggleClock(now, frame, loading);
        Input::DrainHeld();
    } else if (Hotkeys::Pressed(Hotkeys::HOTKEY_TIMER_RESET)) {
        resetEnabled();
        Input::DrainHeld();
    }
}

bool WantsInputBlock(uint32_t held)
{
    if (!anyEnabled())
        return false;
    return (clockEnabled() && Hotkeys::Matches(Hotkeys::HOTKEY_TIMER_TOGGLE, held)) ||
           Hotkeys::Matches(Hotkeys::HOTKEY_TIMER_RESET, held);
}

void OnApplicationStart()
{
    clearAll();
    s_layoutK = 0.0f;
    s_applyPos = s_applySize = true;
}

void ResetToDefaults()
{
    s_frameEnabled = s_loadEnabled = s_igtEnabled = false;
    clearAll();
    s_win.x = kDefaultX;
    s_win.y = kDefaultY;
    s_win.w = kBaseW;
    applyState();
}

void LoadConfig(const cJSON* root)
{
    Config::LoadWindow(root, "practiceTimer", s_win, kMinW, kMaxW);
    bool frame = s_frameEnabled, load = s_loadEnabled, igt = s_igtEnabled;
    Config::ReadBool(root, "practiceTimerFrame", &frame);
    Config::ReadBool(root, "practiceTimerLoad", &load);
    Config::ReadBool(root, "practiceTimerIgt", &igt);
    setFrameEnabled(frame);
    setLoadEnabled(load);
    setIgtEnabled(igt);
    applyState();
}

void SaveConfig(cJSON* root)
{
    Config::SaveWindow(root, "practiceTimer", s_win);
    Config::AddBool(root, "practiceTimerFrame", s_frameEnabled);
    Config::AddBool(root, "practiceTimerLoad", s_loadEnabled);
    Config::AddBool(root, "practiceTimerIgt", s_igtEnabled);
}
}
}
