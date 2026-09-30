#include "hud/hud_input_viewer.h"

#include "core/config.h"
#include "core/input.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"

#include "imgui.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>

namespace Hud {
namespace InputViewer {
static const float kMinW = 240.0f;
static const float kMaxW = 560.0f;
static const float kBaseW = 360.0f;
static const float kBaseH = 210.0f;
static const float kAspect = kBaseW / kBaseH;
static const float kReadoutSize = 15.0f;
static const float kDefaultX = 40.0f;
static const float kDefaultY = 260.0f;
static const float kDefaultW = 330.0f;

static const ImU32 kIdle = IM_COL32(42, 47, 55, 255);
static const ImU32 kHeld = IM_COL32(64, 207, 142, 255);
static const ImU32 kPressed = IM_COL32(255, 214, 92, 255);
static const ImU32 kLitText = IM_COL32(8, 14, 14, 255);
static const ImU32 kText = IM_COL32(225, 231, 239, 255);

static Ui::WindowState s_win = { false, kDefaultX, kDefaultY, kDefaultW, kDefaultW / kAspect };
static float s_opacity = 1.0f;
static bool  s_intReadout = false;
static bool  s_applyPos = false, s_applySize = false;
static float s_layoutK = 0.0f;

struct DrawCtx {
    ImDrawList* dl;
    ImFont* font;
    ImVec2 origin;
    float scale;
};

static ImVec2 p(const DrawCtx& c, float x, float y)
{
    return ImVec2(c.origin.x + x * c.scale, c.origin.y + y * c.scale);
}

static float sc(const DrawCtx& c, float v) { return v * c.scale; }

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static ImU32 buttonColor(bool held, bool pressed)
{
    return pressed ? kPressed : held ? kHeld : kIdle;
}

static void centered(const DrawCtx& c, ImVec2 center, const char* text, ImU32 color, float size)
{
    const float fontSize = sc(c, size);
    const ImVec2 ts = c.font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    DrawText(c.dl, c.font, fontSize, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
             color, text, Config::g_settings.boldLetters);
}

static void drawButton(const DrawCtx& c, float x, float y, float r, const char* label,
                       bool held, bool pressed)
{
    const ImVec2 center = p(c, x, y);
    c.dl->AddCircleFilled(center, sc(c, r), buttonColor(held, pressed), 24);
    c.dl->AddCircle(center, sc(c, r), IM_COL32(208, 216, 226, 180), 24, sc(c, 1.5f));
    centered(c, center, label, held || pressed ? kLitText : kText, r * 0.85f);
}

static void drawPill(const DrawCtx& c, float x1, float y1, float x2, float y2,
                     const char* label, bool held, bool pressed)
{
    const ImVec2 min = p(c, x1, y1), max = p(c, x2, y2);
    c.dl->AddRectFilled(min, max, buttonColor(held, pressed), sc(c, 6.0f));
    c.dl->AddRect(min, max, IM_COL32(208, 216, 226, 165), sc(c, 6.0f), 0, sc(c, 1.4f));
    centered(c, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), label,
             held || pressed ? kLitText : kText, 11.0f);
}

static void drawStick(const DrawCtx& c, float x, float y, float sx, float sy,
                      bool clicked, bool pressed)
{
    const ImVec2 center = p(c, x, y);
    const float radius = sc(c, 18.0f);
    c.dl->AddCircleFilled(center, radius,
                          clicked ? IM_COL32(28, 70, 52, 255) : IM_COL32(26, 31, 39, 255), 32);
    c.dl->AddCircle(center, radius,
                    pressed ? kPressed : clicked ? kHeld : IM_COL32(182, 194, 210, 150),
                    32, sc(c, clicked ? 2.7f : 1.5f));
    c.dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y),
                  IM_COL32(130, 142, 158, 75));
    c.dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius),
                  IM_COL32(130, 142, 158, 75));
    sx = clampf(sx, -1.0f, 1.0f);
    sy = clampf(sy, -1.0f, 1.0f);
    const float dotR = sc(c, 4.3f);
    const ImVec2 dot(center.x + sx * (radius - dotR - sc(c, 2.5f)),
                     center.y - sy * (radius - dotR - sc(c, 2.5f)));
    c.dl->AddCircleFilled(dot, dotR,
                          pressed ? kPressed : clicked ? kHeld : IM_COL32(75, 192, 235, 255), 20);
}

static void dpadPart(const DrawCtx& c, float x1, float y1, float x2, float y2,
                     bool held, bool pressed)
{
    c.dl->AddRectFilled(p(c, x1, y1), p(c, x2, y2), buttonColor(held, pressed), sc(c, 3.0f));
}

static void drawDpad(const DrawCtx& c, float x, float y, uint32_t held, uint32_t pressed)
{
    dpadPart(c, x - 5.5f, y - 22.0f, x + 5.5f, y - 4.0f,
             held & Input::BTN_UP, pressed & Input::BTN_UP);
    dpadPart(c, x - 5.5f, y + 4.0f, x + 5.5f, y + 22.0f,
             held & Input::BTN_DOWN, pressed & Input::BTN_DOWN);
    dpadPart(c, x - 22.0f, y - 5.5f, x - 4.0f, y + 5.5f,
             held & Input::BTN_LEFT, pressed & Input::BTN_LEFT);
    dpadPart(c, x + 4.0f, y - 5.5f, x + 22.0f, y + 5.5f,
             held & Input::BTN_RIGHT, pressed & Input::BTN_RIGHT);
}

static const char* sourceName(uint32_t mask)
{
    if (!mask) return "No controller";
    if ((mask & (mask - 1)) != 0) return "Mixed";
    if (mask & Input::SOURCE_GAMEPAD) return "GamePad";
    if (mask & Input::SOURCE_PRO) return "Pro Controller";
    if (mask & Input::SOURCE_CLASSIC) return "Classic Controller";
    return "Controller";
}

static int toPadUnits(float v)
{
    const int i = (int)(v * 127.0f + (v >= 0.0f ? 0.5f : -0.5f));
    return i < -127 ? -127 : (i > 127 ? 127 : i);
}

static void formatStick(char* buf, size_t n, char label, float x, float y)
{
    if (s_intReadout)
        snprintf(buf, n, "%c  %d, %d", label, toPadUnits(x), toPadUnits(y));
    else
        snprintf(buf, n, "%c  %.4f, %.4f", label, (double)x, (double)y);
}

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    const float ratio = *(const float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

static void applyState()
{
    s_win.w = clampf(s_win.w, kMinW, kMaxW);
    s_win.h = s_win.w / kAspect;
    s_applyPos = s_applySize = true;
}

Ui::WindowState& State() { return s_win; }

float GetOpacity() { return s_opacity; }
void SetOpacity(float opacity) { s_opacity = clampf(opacity, 0.0f, 1.0f); }

bool GetIntReadout() { return s_intReadout; }
void SetIntReadout(bool on) { s_intReadout = on; }

void ResetToDefaults()
{
    s_opacity = 1.0f;
    s_intReadout = false;
    s_win = { false, kDefaultX, kDefaultY, kDefaultW, kDefaultW / kAspect };
    applyState();
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
    const Input::Snapshot& snap = Input::Current();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinW * k, kMinW * k / kAspect),
                                        ImVec2(kMaxW * k, kMaxW * k / kAspect),
                                        aspectConstraint, (void*)&kAspect);
    ImGui::SetNextWindowPos(ImVec2(s_win.x * k, s_win.y * k),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_win.w * k, s_win.h * k),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = s_applySize = false;
    const float background = Config::g_settings.overlayOpacity;
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.42f * background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * k, style.FramePadding.y * k));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    const bool wasEnabled = s_win.enabled;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    const bool open = ImGui::Begin("Input Viewer", menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / kBaseH < scale)
            scale = avail.y / kBaseH;
        DrawCtx c = { ImGui::GetWindowDrawList(), ImGui::GetFont(), ImVec2(), scale };
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        c.origin = ImVec2(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                          cursor.y + (avail.y - kBaseH * scale) * 0.5f);
        c.dl->AddRectFilled(c.origin, p(c, kBaseW, kBaseH),
                            IM_COL32(7, 10, 14, (int)(190.0f * background + 0.5f)), sc(c, 9.0f));
        c.dl->AddRectFilled(p(c, 42.0f, 67.0f), p(c, 318.0f, 177.0f),
                            IM_COL32(24, 29, 37, (int)(238.0f * s_opacity + 0.5f)), sc(c, 45.0f));

        const char* source = sourceName(snap.sourceMask);
        const float fs = sc(c, 10.0f);
        const ImVec2 sz = c.font->CalcTextSizeA(fs, FLT_MAX, 0.0f, source);
        DrawText(c.dl, c.font, fs, ImVec2(p(c, 345.0f, 11.0f).x - sz.x, p(c, 0.0f, 11.0f).y),
                 snap.valid ? IM_COL32(116, 220, 164, 255) : IM_COL32(170, 176, 185, 255),
                 source, Config::g_settings.boldLetters);

        const uint32_t held = snap.held, pressed = snap.pressed;
        drawPill(c, 67, 56, 112, 68, "ZL", held & Input::BTN_ZL, pressed & Input::BTN_ZL);
        drawPill(c, 248, 56, 293, 68, "ZR", held & Input::BTN_ZR, pressed & Input::BTN_ZR);
        drawPill(c, 79, 70, 119, 82, "L", held & Input::BTN_L, pressed & Input::BTN_L);
        drawPill(c, 241, 70, 281, 82, "R", held & Input::BTN_R, pressed & Input::BTN_R);
        drawStick(c, 87, 105, snap.lx, snap.ly, held & Input::BTN_L3, pressed & Input::BTN_L3);
        drawStick(c, 273, 105, snap.rx, snap.ry, held & Input::BTN_R3, pressed & Input::BTN_R3);
        drawDpad(c, 125, 142, held, pressed);
        drawButton(c, 244, 127, 8.5f, "X", held & Input::BTN_X, pressed & Input::BTN_X);
        drawButton(c, 226, 142, 8.5f, "Y", held & Input::BTN_Y, pressed & Input::BTN_Y);
        drawButton(c, 261, 142, 8.5f, "A", held & Input::BTN_A, pressed & Input::BTN_A);
        drawButton(c, 244, 159, 8.5f, "B", held & Input::BTN_B, pressed & Input::BTN_B);
        drawPill(c, 156, 98, 174, 110, "-", held & Input::BTN_MINUS, pressed & Input::BTN_MINUS);
        drawPill(c, 186, 98, 204, 110, "+", held & Input::BTN_PLUS, pressed & Input::BTN_PLUS);

        char readout[48];
        const ImU32 readoutColor = IM_COL32(150, 205, 240, 235);
        formatStick(readout, sizeof(readout), 'L', snap.lx, snap.ly);
        centered(c, p(c, 87, 193), readout, readoutColor, kReadoutSize);
        formatStick(readout, sizeof(readout), 'R', snap.rx, snap.ry);
        centered(c, p(c, 273, 193), readout, readoutColor, kReadoutSize);
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) ||
                       moved(oldY, s_win.y) || moved(oldW, s_win.w) || moved(oldH, s_win.h)))
        Config::MarkDirty();
}

void LoadConfig(const cJSON* root)
{
    Config::LoadWindow(root, "inputViewer", s_win, kMinW, kMaxW);
    Config::ReadFloat(root, "inputViewerOpacity", &s_opacity, 0.0f, 1.0f);
    Config::ReadBool(root, "inputViewerInts", &s_intReadout);
    applyState();
}

void SaveConfig(cJSON* root)
{
    Config::SaveWindow(root, "inputViewer", s_win);
    Config::AddNumber(root, "inputViewerOpacity", (double)s_opacity);
    Config::AddBool(root, "inputViewerInts", s_intReadout);
}
}
}
