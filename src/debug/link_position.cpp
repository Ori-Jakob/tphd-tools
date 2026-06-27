// link_position.cpp -- see link_position.h.
#include "link_position.h"

#include "imgui.h"
#include "overlay.h"
#include "game/game.h"     // dComIfGp_getPlayer(), fopAc_ac_c
#include "game/z2_audio.h" // Z2GetGameClock()

#include <coreinit/time.h>
#include <float.h>
#include <stdio.h>

namespace Debug {
namespace LinkPosition {

static const float kBaseW = 164.0f;
static const float kBaseHCompact = 140.0f;
static const float kBaseHRealDateTime = 184.0f;
static const float kDefaultW = 164.0f;
static const float kDefaultH = kDefaultW / (kBaseW / kBaseHCompact);
static const float kOldDefaultW = 180.0f;
static const float kOldDefaultH = 168.0f;
static const float kMinW = 130.0f;
static const float kMaxW = 360.0f;
static const float kValueRightX = 152.0f;

static bool  s_enabled = false;
static bool  s_showRealDateTime = false;
static float s_posX = 40.0f, s_posY = 90.0f;   // latest / desired window position
static float s_width = kDefaultW, s_height = kDefaultH;
static bool  s_applyPos = false;               // apply s_pos via SetNextWindowPos(Always)
static bool  s_applySize = false;
static OSTime s_sessionStart = 0;

struct DrawCtx {
    ImDrawList* dl;
    ImFont* font;
    ImVec2 origin;
    float scale;
};

static float getBaseHeight()
{
    return s_showRealDateTime ? kBaseHRealDateTime : kBaseHCompact;
}

static float getAspect()
{
    return kBaseW / getBaseHeight();
}

static void setRealDateTimeEnabled(bool enabled)
{
    if (s_showRealDateTime == enabled)
        return;

    float oldHeight = s_height;
    s_showRealDateTime = enabled;
    s_height = s_width / getAspect();

    // Keep the bottom edge anchored while the optional rows appear/disappear.
    s_posY -= s_height - oldHeight;
    s_applyPos = true;
    s_applySize = true;
}

void DrawMenuItem()
{
    ImGui::Checkbox("Game Info", &s_enabled);
    if (s_enabled) {
        ImGui::Indent();
        bool showRealDateTime = s_showRealDateTime;
        if (ImGui::Checkbox("Show real date/time", &showRealDateTime))
            setRealDateTimeEnabled(showRealDateTime);
        ImGui::Unindent();
    }
}

bool IsEnabled()           { return s_enabled; }
void SetEnabled(bool e)    { s_enabled = e; }
bool IsRealDateTimeEnabled() { return s_showRealDateTime; }
void SetRealDateTimeEnabled(bool e) { setRealDateTimeEnabled(e); }

void GetWindowPos(float* x, float* y) { *x = s_posX; *y = s_posY; }
void SetWindowPos(float x, float y)   { s_posX = x; s_posY = y; s_applyPos = true; }

void GetWindowSize(float* w, float* h) { *w = s_width; *h = s_height; }
void SetWindowSize(float w, float h)
{
    if (w > kOldDefaultW - 1.0f && w < kOldDefaultW + 1.0f &&
        h > kOldDefaultH - 1.0f && h < kOldDefaultH + 1.0f) {
        w = kDefaultW;
        h = kDefaultH;
    }

    if (w < kMinW)
        w = kMinW;
    if (w > kMaxW)
        w = kMaxW;
    s_width = w;
    s_height = w / getAspect();
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

// ImGui has no native text outline, so when "Bold letters" is on we fake a stroke
// by drawing the glyphs in black at the 8 neighbouring offsets, then the fill
// colour on top. The outline alpha tracks the fill's alpha so it fades together.
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

static void addText(const DrawCtx& ctx, float x, float y, const char* text, ImU32 color,
                    float size = 10.0f, bool outline = false)
{
    drawGlyphs(ctx, p(ctx, x, y), text, color, sc(ctx, size), outline);
}

static void addRightText(const DrawCtx& ctx, float x, float y, const char* text, ImU32 color,
                         float size = 10.0f, bool outline = false)
{
    float fontSize = sc(ctx, size);
    ImVec2 ts = ctx.font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    drawGlyphs(ctx, ImVec2(p(ctx, x, y).x - ts.x, p(ctx, x, y).y), text, color, fontSize, outline);
}

static void addPair(const DrawCtx& ctx, float y, const char* label, const char* value,
                    ImU32 labelColor, ImU32 valueColor, float size = 9.3f)
{
    addText(ctx, 12.0f, y, label, labelColor, size, ov::g_settings.boldLetters);
    addRightText(ctx, kValueRightX, y, value, valueColor, size, ov::g_settings.boldLetters);
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

static void formatGameClock(char* out, int outSize)
{
    u8 hour = 0;
    u8 minute = 0;
    if (Z2GetGameClock(&hour, &minute))
        snprintf(out, outSize, "%02u:%02u", (unsigned)hour, (unsigned)minute);
    else
        snprintf(out, outSize, "--:--");
}

static void ensureSessionStart()
{
    OSTime now = OSGetTime();
    if (s_sessionStart == 0 || now < s_sessionStart)
        s_sessionStart = now;
}

static void formatSessionTime(char* out, int outSize)
{
    ensureSessionStart();

    OSTime now = OSGetTime();
    uint64_t elapsed = OSTicksToSeconds((uint64_t)(now - s_sessionStart));
    uint64_t hours = elapsed / 3600;
    uint64_t minutes = (elapsed / 60) % 60;
    uint64_t seconds = elapsed % 60;

    if (hours < 100)
        snprintf(out, outSize, "%02llu:%02llu:%02llu",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
    else
        snprintf(out, outSize, "%llu:%02llu:%02llu",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
}

static void formatRealDate(char* out, int outSize)
{
    OSCalendarTime ct;
    OSTicksToCalendarTime(OSGetTime(), &ct);
    snprintf(out, outSize, "%04d-%02d-%02d", ct.tm_year, ct.tm_mon + 1, ct.tm_mday);
}

static void formatRealTime(char* out, int outSize)
{
    OSCalendarTime ct;
    OSTicksToCalendarTime(OSGetTime(), &ct);
    snprintf(out, outSize, "%02d:%02d:%02d", ct.tm_hour, ct.tm_min, ct.tm_sec);
}

void DrawWindow(bool menuActive)
{
    ensureSessionStart();

    if (!s_enabled)
        return;

    // Title bar (and its close X) only while the menu is open; a clean locked HUD
    // otherwise. The X clears s_enabled (and unticks the Debug checkbox).
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    // Black background at half opacity so the text stays readable over the game.
    float baseHeight = getBaseHeight();
    float aspect = kBaseW / baseHeight;
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinW, kMinW / aspect),
                                        ImVec2(kMaxW, kMaxW / aspect),
                                        aspectConstraint, &aspect);
    float overlayOpacity = ov::g_settings.overlayOpacity;
    ImGui::SetNextWindowBgAlpha(0.5f * overlayOpacity);
    // Restore the saved position (Always, one-shot) or seed it the first time.
    ImGui::SetNextWindowPos(ImVec2(s_posX, s_posY),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_width, s_height),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = false;
    s_applySize = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    bool open = ImGui::Begin("Game Info", menuActive ? &s_enabled : nullptr, flags);
    // Track the live position so dragging/resizing persists.
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    s_posX = wp.x;
    s_posY = wp.y;
    s_width = ws.x;
    s_height = ws.y;
    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        float sy = avail.y / baseHeight;
        if (sy < scale)
            scale = sy;

        DrawCtx ctx;
        ctx.dl = ImGui::GetWindowDrawList();
        ctx.font = ImGui::GetFont();
        ctx.scale = scale;
        ctx.origin = ImVec2(ImGui::GetCursorScreenPos().x + (avail.x - kBaseW * scale) * 0.5f,
                            ImGui::GetCursorScreenPos().y + (avail.y - baseHeight * scale) * 0.5f);

        ctx.dl->AddRectFilled(ctx.origin, p(ctx, kBaseW, baseHeight),
                              IM_COL32(0, 0, 0, (int)(115.0f * overlayOpacity)),
                              sc(ctx, 6.0f));

        const ImU32 label = IM_COL32(180, 190, 204, 210);
        const ImU32 value = IM_COL32(255, 255, 255, 255);   // pure white + black stroke (addPair)
        const ImU32 disabled = IM_COL32(156, 164, 176, 190);

        char bufTime[16];
        char bufSession[32];
        char bufAngle[16];
        char bufYAngle[16];
        char bufSpeed[32];
        char bufX[32];
        char bufY[32];
        char bufZ[32];
        char bufAction[16];
        char bufRealDate[24];
        char bufRealTime[16];

        formatGameClock(bufTime, sizeof(bufTime));
        formatSessionTime(bufSession, sizeof(bufSession));

        fopAc_ac_c* link = dComIfGp_getPlayer();
        if (!link) {
            snprintf(bufAngle, sizeof(bufAngle), "--");
            snprintf(bufYAngle, sizeof(bufYAngle), "--");
            snprintf(bufSpeed, sizeof(bufSpeed), "--");
            snprintf(bufX, sizeof(bufX), "--");
            snprintf(bufY, sizeof(bufY), "--");
            snprintf(bufZ, sizeof(bufZ), "--");
            snprintf(bufAction, sizeof(bufAction), "--");
        } else {
            const cXyz& pos = link->current.pos;
            snprintf(bufAngle, sizeof(bufAngle), "%u", (unsigned)(u16)link->shape_angle.y);
            snprintf(bufYAngle, sizeof(bufYAngle), "%d", (int)daPy_getLookAngleY(link));
            snprintf(bufSpeed, sizeof(bufSpeed), "%.7f", link->speedF);
            snprintf(bufX, sizeof(bufX), "%.7f", pos.x);
            snprintf(bufY, sizeof(bufY), "%.7f", pos.y);
            snprintf(bufZ, sizeof(bufZ), "%.7f", pos.z);
            snprintf(bufAction, sizeof(bufAction), "%u", (unsigned)daAlink_getActionID(link));
        }

        addPair(ctx, 25.0f, "Current Session:", bufSession, label, value);
        addPair(ctx, 12.0f, "ToD:", bufTime, label, value);
        addPair(ctx, 40.0f, "Angle:", bufAngle, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 53.0f, "Y-Angle:", bufYAngle, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 66.0f, "Speed:", bufSpeed, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 79.0f, "X:", bufX, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 92.0f, "Y:", bufY, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 105.0f, "Z:", bufZ, link ? label : disabled, link ? value : disabled);
        addPair(ctx, 118.0f, "Action:", bufAction, link ? label : disabled, link ? value : disabled);

        if (s_showRealDateTime) {
            formatRealDate(bufRealDate, sizeof(bufRealDate));
            formatRealTime(bufRealTime, sizeof(bufRealTime));
            addPair(ctx, 140.0f, "Date:", bufRealDate, label, value);
            addPair(ctx, 153.0f, "Clock:", bufRealTime, label, value);
        }

        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace LinkPosition
} // namespace Debug
