// input_viewer.cpp -- see tools/input_viewer.h.
#include "tools/input_viewer.h"

#include "input.h"
#include "overlay.h"

#include "imgui.h"

#include <float.h>
#include <stdio.h>

namespace Tools {
namespace InputViewer {

static const float kBaseW = 360.0f;
static const float kBaseH = 210.0f;
static const float kAspect = kBaseW / kBaseH;
static const float kDefaultW = 330.0f;
static const float kDefaultH = kDefaultW / kAspect;
static const float kMinW = 240.0f;
static const float kMaxW = 560.0f;

static bool  s_enabled = false;
static float s_opacity = 1.0f;
static float s_posX = 40.0f, s_posY = 260.0f;
static float s_width = kDefaultW, s_height = kDefaultH;
static bool  s_applyPos = false;
static bool  s_applySize = false;

struct DrawCtx {
    ImDrawList* dl;
    ImFont* font;
    ImVec2 origin;
    float scale;
};

void DrawMenuItem()
{
    ImGui::Checkbox("Input Viewer", &s_enabled);
    if (s_enabled) {
        ImGui::Indent();
        int opacityPercent = (int)(s_opacity * 100.0f + 0.5f);
        if (ImGui::SliderInt("Opacity##InputViewer", &opacityPercent, 0, 100, "%d%%"))
            SetOpacity(opacityPercent / 100.0f);
        ImGui::Unindent();
    }
}

bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }
float GetOpacity()      { return s_opacity; }

void SetOpacity(float opacity)
{
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    s_opacity = opacity;
}

void GetWindowPos(float* x, float* y)
{
    *x = s_posX;
    *y = s_posY;
}

void SetWindowPos(float x, float y)
{
    s_posX = x;
    s_posY = y;
    s_applyPos = true;
}

void GetWindowSize(float* w, float* h)
{
    *w = s_width;
    *h = s_height;
}

void SetWindowSize(float w, float h)
{
    if (h > 0.0f && h * kAspect > w)
        w = h * kAspect;
    if (w < kMinW)
        w = kMinW;
    if (w > kMaxW)
        w = kMaxW;
    s_width = w;
    s_height = w / kAspect;
    s_applySize = true;
}

static ImVec2 p(const DrawCtx& ctx, float x, float y)
{
    return ImVec2(ctx.origin.x + x * ctx.scale, ctx.origin.y + y * ctx.scale);
}

static float sc(const DrawCtx& ctx, float v)
{
    return v * ctx.scale;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static ImU32 colorFor(bool down, bool pressed)
{
    if (pressed)
        return IM_COL32(255, 214, 92, 255);
    if (down)
        return IM_COL32(64, 207, 142, 255);
    return IM_COL32(42, 47, 55, 255);
}

// static void addText(const DrawCtx& ctx, float x, float y, const char* text, ImU32 color,
//                     float size = 12.0f)
// {
//     ctx.dl->AddText(ctx.font, sc(ctx, size), p(ctx, x, y), color, text);
// }

static void drawGlyphs(const DrawCtx& ctx, ImVec2 pos, const char* text, ImU32 color,
                       float fontSize, bool outline)
{
    if (outline) {
        float o = sc(ctx, 1.0f);
        if (o < 1.0f)
            o = 1.0f;
        const ImU32 black = IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xFF);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    ctx.dl->AddText(ctx.font, fontSize, ImVec2(pos.x + dx * o, pos.y + dy * o),
                                    black, text);
    }
    ctx.dl->AddText(ctx.font, fontSize, pos, color, text);
}

static void addCenteredText(const DrawCtx& ctx, ImVec2 center, const char* text, ImU32 color,
                            float size = 12.0f, bool outline = false)
{
    float fontSize = sc(ctx, size);
    ImVec2 ts = ctx.font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    drawGlyphs(ctx, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
               text, color, fontSize, outline);
}

static void drawButton(const DrawCtx& ctx, float x, float y, float r, const char* label,
                       bool down, bool pressed)
{
    ImVec2 c = p(ctx, x, y);
    float rr = sc(ctx, r);
    ImU32 fill = colorFor(down, pressed);
    ctx.dl->AddCircleFilled(c, rr, fill, 32);
    ctx.dl->AddCircle(c, rr, IM_COL32(208, 216, 226, 180), 32, sc(ctx, 1.6f));
    addCenteredText(ctx, c, label, down || pressed ? IM_COL32(8, 14, 14, 255)
                                                   : IM_COL32(220, 226, 236, 255),
                    r * 0.85f);
}

static void drawPill(const DrawCtx& ctx, float x1, float y1, float x2, float y2,
                     const char* label, bool down, bool pressed)
{
    ImVec2 min = p(ctx, x1, y1);
    ImVec2 max = p(ctx, x2, y2);
    float rounding = sc(ctx, 7.0f);
    ImU32 fill = colorFor(down, pressed);
    ctx.dl->AddRectFilled(min, max, fill, rounding);
    ctx.dl->AddRect(min, max, IM_COL32(208, 216, 226, 165), rounding, 0, sc(ctx, 1.6f));
    addCenteredText(ctx, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), label,
                    down || pressed ? IM_COL32(8, 14, 14, 255)
                                    : IM_COL32(220, 226, 236, 255),
                    12.0f);
}

static void drawStick(const DrawCtx& ctx, float x, float y, float r, float sx, float sy,
                      bool clicked, bool clickPressed)
{
    ImVec2 c = p(ctx, x, y);
    float rr = sc(ctx, r);
    ImU32 wellFill = clicked ? IM_COL32(28, 70, 52, 255) : IM_COL32(26, 31, 39, 255);
    ImU32 wellRing = clickPressed ? IM_COL32(255, 214, 92, 255) :
                    clicked      ? IM_COL32(64, 207, 142, 255) :
                                   IM_COL32(182, 194, 210, 135);
    ctx.dl->AddCircleFilled(c, rr, wellFill, 40);
    ctx.dl->AddCircle(c, rr, wellRing, 40, clicked ? sc(ctx, 2.7f) : sc(ctx, 1.6f));
    ctx.dl->AddLine(ImVec2(c.x - rr, c.y), ImVec2(c.x + rr, c.y), IM_COL32(130, 142, 158, 75));
    ctx.dl->AddLine(ImVec2(c.x, c.y - rr), ImVec2(c.x, c.y + rr), IM_COL32(130, 142, 158, 75));

    sx = clampf(sx, -1.0f, 1.0f);
    sy = clampf(sy, -1.0f, 1.0f);
    float dotR = sc(ctx, r * 0.24f);
    ImVec2 dot(c.x + sx * (rr - dotR - sc(ctx, 3.0f)),
               c.y - sy * (rr - dotR - sc(ctx, 3.0f)));
    ImU32 dotColor = clickPressed ? IM_COL32(255, 214, 92, 255) :
                     clicked      ? IM_COL32(64, 207, 142, 255) :
                                    IM_COL32(75, 192, 235, 255);
    ctx.dl->AddCircleFilled(dot, dotR, dotColor, 24);
    ctx.dl->AddCircle(dot, dotR, IM_COL32(235, 250, 255, 185), 24, sc(ctx, 1.2f));
}

static void drawDpadPart(const DrawCtx& ctx, float x1, float y1, float x2, float y2,
                         bool down, bool pressed)
{
    ImVec2 min = p(ctx, x1, y1);
    ImVec2 max = p(ctx, x2, y2);
    ctx.dl->AddRectFilled(min, max, colorFor(down, pressed), sc(ctx, 4.0f));
    ctx.dl->AddRect(min, max, IM_COL32(208, 216, 226, 145), sc(ctx, 4.0f), 0, sc(ctx, 1.1f));
}

static void drawDpad(const DrawCtx& ctx, float x, float y, uint32_t buttons, uint32_t pressed)
{
    ctx.dl->AddRectFilled(p(ctx, x - 7.0f, y - 7.0f), p(ctx, x + 7.0f, y + 7.0f),
                          IM_COL32(42, 47, 55, 255), sc(ctx, 3.0f));
    drawDpadPart(ctx, x - 5.5f, y - 22.0f, x + 5.5f, y - 5.0f,
                 (buttons & ov::MB_UP) != 0, (pressed & ov::MB_UP) != 0);
    drawDpadPart(ctx, x - 5.5f, y + 5.0f, x + 5.5f, y + 22.0f,
                 (buttons & ov::MB_DOWN) != 0, (pressed & ov::MB_DOWN) != 0);
    drawDpadPart(ctx, x - 22.0f, y - 5.5f, x - 5.0f, y + 5.5f,
                 (buttons & ov::MB_LEFT) != 0, (pressed & ov::MB_LEFT) != 0);
    drawDpadPart(ctx, x + 5.0f, y - 5.5f, x + 22.0f, y + 5.5f,
                 (buttons & ov::MB_RIGHT) != 0, (pressed & ov::MB_RIGHT) != 0);
}

static void drawControllerBody(const DrawCtx& ctx)
{
    ImDrawList* dl = ctx.dl;
    dl->PathClear();
    dl->PathLineTo(p(ctx, 42.0f, 120.0f));
    dl->PathBezierCubicCurveTo(p(ctx, 43.0f, 80.0f), p(ctx, 82.0f, 64.0f), p(ctx, 124.0f, 72.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 151.0f, 77.0f), p(ctx, 209.0f, 77.0f), p(ctx, 236.0f, 72.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 278.0f, 64.0f), p(ctx, 317.0f, 80.0f), p(ctx, 318.0f, 120.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 321.0f, 164.0f), p(ctx, 286.0f, 187.0f), p(ctx, 244.0f, 170.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 215.0f, 158.0f), p(ctx, 145.0f, 158.0f), p(ctx, 116.0f, 170.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 74.0f, 187.0f), p(ctx, 39.0f, 164.0f), p(ctx, 42.0f, 120.0f), 16);
    dl->PathFillConvex(IM_COL32(24, 29, 37, (int)(238.0f * s_opacity + 0.5f)));

    dl->PathClear();
    dl->PathLineTo(p(ctx, 42.0f, 120.0f));
    dl->PathBezierCubicCurveTo(p(ctx, 43.0f, 80.0f), p(ctx, 82.0f, 64.0f), p(ctx, 124.0f, 72.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 151.0f, 77.0f), p(ctx, 209.0f, 77.0f), p(ctx, 236.0f, 72.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 278.0f, 64.0f), p(ctx, 317.0f, 80.0f), p(ctx, 318.0f, 120.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 321.0f, 164.0f), p(ctx, 286.0f, 187.0f), p(ctx, 244.0f, 170.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 215.0f, 158.0f), p(ctx, 145.0f, 158.0f), p(ctx, 116.0f, 170.0f), 16);
    dl->PathBezierCubicCurveTo(p(ctx, 74.0f, 187.0f), p(ctx, 39.0f, 164.0f), p(ctx, 42.0f, 120.0f), 16);
    dl->PathStroke(IM_COL32(190, 200, 214, 135), ImDrawFlags_Closed, sc(ctx, 2.0f));
}

static const char* sourceName(uint32_t mask)
{
    if (mask == 0)
        return "No controller";
    if ((mask & (mask - 1)) != 0)
        return "Mixed";
    if (mask & Input::SOURCE_GAMEPAD)
        return "GamePad";
    if (mask & Input::SOURCE_PRO)
        return "Pro Controller";
    if (mask & Input::SOURCE_CLASSIC)
        return "Classic Controller";
    return "Controller";
}

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    float ratio = *(float*)data->UserData;
    float w = data->DesiredSize.x;
    float h = data->DesiredSize.y;
    if (w / ratio > h)
        h = w / ratio;
    else
        w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;

    Input::Snapshot snap;
    Input::GetSnapshot(&snap);

    // With the menu open the viewer gets a real title bar so it carries a close (X)
    // button -- clicking it clears s_enabled (and unticks the Tools checkbox). With
    // the menu closed it's a clean, locked, title-bar-less HUD (no X).
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinW, kMinW / kAspect),
                                        ImVec2(kMaxW, kMaxW / kAspect),
                                        aspectConstraint, (void*)&kAspect);
    float overlayOpacity = ov::g_settings.overlayOpacity;
    ImGui::SetNextWindowBgAlpha(0.42f * overlayOpacity);
    ImGui::SetNextWindowPos(ImVec2(s_posX, s_posY),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_width, s_height),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = false;
    s_applySize = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    bool open = ImGui::Begin("Input Viewer", menuActive ? &s_enabled : nullptr, flags);
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    s_posX = wp.x;
    s_posY = wp.y;
    s_width = ws.x;
    s_height = ws.y;

    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        float sy = avail.y / kBaseH;
        if (sy < scale)
            scale = sy;
        DrawCtx ctx;
        ctx.dl = ImGui::GetWindowDrawList();
        ctx.font = ImGui::GetFont();
        ctx.scale = scale;
        ctx.origin = ImVec2(ImGui::GetCursorScreenPos().x + (avail.x - kBaseW * scale) * 0.5f,
                            ImGui::GetCursorScreenPos().y + (avail.y - kBaseH * scale) * 0.5f);

        ctx.dl->AddRectFilled(ctx.origin, p(ctx, kBaseW, kBaseH),
                              IM_COL32(7, 10, 14, (int)(160.0f * overlayOpacity)),
                              sc(ctx, 9.0f));

        // addText(ctx, 15.0f, 10.0f, "Input Viewer", IM_COL32(235, 240, 248, 235), 11.0f);
        const bool boldLetters = ov::g_settings.boldLetters;
        ImVec2 sourceSz = ctx.font->CalcTextSizeA(sc(ctx, 10.0f), FLT_MAX, 0.0f,
                                                  sourceName(snap.sourceMask));
        drawGlyphs(ctx, p(ctx, kBaseW - 15.0f - sourceSz.x / ctx.scale, 12.0f),
                   sourceName(snap.sourceMask),
                   snap.connected ? IM_COL32(116, 220, 164, 255)
                                  : IM_COL32(170, 176, 185, 255),
                   sc(ctx, 10.0f), boldLetters);

        // Controller graphic is shifted up 15px (gctx) to free a strip beneath the
        // body for the analog-stick readouts, which stay on the unshifted ctx.
        DrawCtx gctx = ctx;
        gctx.origin.y -= 15.0f;

        drawControllerBody(gctx);

        drawPill(gctx, 67.0f, 56.0f, 112.0f, 68.0f, "ZL",
                 (snap.buttons & ov::MB_ZL) != 0, (snap.pressed & ov::MB_ZL) != 0);
        drawPill(gctx, 248.0f, 56.0f, 293.0f, 68.0f, "ZR",
                 (snap.buttons & ov::MB_ZR) != 0, (snap.pressed & ov::MB_ZR) != 0);
        drawPill(gctx, 79.0f, 70.0f, 119.0f, 82.0f, "L",
                 (snap.buttons & ov::MB_L) != 0, (snap.pressed & ov::MB_L) != 0);
        drawPill(gctx, 241.0f, 70.0f, 281.0f, 82.0f, "R",
                 (snap.buttons & ov::MB_R) != 0, (snap.pressed & ov::MB_R) != 0);

        drawStick(gctx, 87.0f, 104.0f, 18.0f, snap.leftX, snap.leftY,
                  (snap.buttons & ov::MB_LSTICK) != 0, (snap.pressed & ov::MB_LSTICK) != 0);
        drawDpad(gctx, 125.0f, 142.0f, snap.buttons, snap.pressed);
        drawStick(gctx, 273.0f, 104.0f, 18.0f, snap.rightX, snap.rightY,
                  (snap.buttons & ov::MB_RSTICK) != 0, (snap.pressed & ov::MB_RSTICK) != 0);

        drawButton(gctx, 244.0f, 127.0f, 8.5f, "X",
                   (snap.buttons & ov::MB_X) != 0, (snap.pressed & ov::MB_X) != 0);
        drawButton(gctx, 226.0f, 142.0f, 8.5f, "Y",
                   (snap.buttons & ov::MB_Y) != 0, (snap.pressed & ov::MB_Y) != 0);
        drawButton(gctx, 261.0f, 142.0f, 8.5f, "A",
                   (snap.buttons & ov::MB_A) != 0, (snap.pressed & ov::MB_A) != 0);
        drawButton(gctx, 244.0f, 159.0f, 8.5f, "B",
                   (snap.buttons & ov::MB_B) != 0, (snap.pressed & ov::MB_B) != 0);

        drawPill(gctx, 156.0f, 98.0f, 174.0f, 110.0f, "-",
                 (snap.buttons & ov::MB_MINUS) != 0, (snap.pressed & ov::MB_MINUS) != 0);
        drawPill(gctx, 186.0f, 98.0f, 204.0f, 110.0f, "+",
                 (snap.buttons & ov::MB_PLUS) != 0, (snap.pressed & ov::MB_PLUS) != 0);

        // Analog-stick readouts under each stick (rounded to 4 decimals).
        const ImU32 kStickText = IM_COL32(150, 205, 240, 235);
        char lbuf[48], rbuf[48];
        snprintf(lbuf, sizeof(lbuf), "L  %.4f, %.4f", snap.leftX, snap.leftY);
        snprintf(rbuf, sizeof(rbuf), "R  %.4f, %.4f", snap.rightX, snap.rightY);
        addCenteredText(ctx, p(ctx, 87.0f, 193.0f), lbuf, kStickText, 11.0f, boldLetters);
        addCenteredText(ctx, p(ctx, 273.0f, 193.0f), rbuf, kStickText, 11.0f, boldLetters);

        ImGui::Dummy(avail);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace InputViewer
} // namespace Tools
