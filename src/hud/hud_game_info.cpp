#include "hud/hud_game_info.h"

#include "core/config.h"
#include "core/settings.h"
#include "hud/hud_text.h"
#include "render/renderer.h"
#include "ui/ui_window.h"

#include "SSystem/SComponent/c_counter.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"

#include "cJSON.h"
#include "imgui.h"

#include <coreinit/time.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Hud {
namespace GameInfo {
enum Row {
    ROW_TIME_OF_DAY = 0,
    ROW_CURRENT_SESSION,
    ROW_ANGLE,
    ROW_Y_ANGLE,
    ROW_SPEED,
    ROW_X,
    ROW_Y,
    ROW_Z,
    ROW_ACTION,
    ROW_FRAME,
    ROW_DATE,
    ROW_CLOCK,
    ROW_COUNT
};

static const uint32_t kAllRows = (1u << ROW_COUNT) - 1u;
static const uint32_t kDefaultRows = kAllRows & ~((1u << ROW_DATE) | (1u << ROW_CLOCK));
static const uint32_t kPlayerRows = (1u << ROW_ANGLE) | (1u << ROW_Y_ANGLE) |
                                    (1u << ROW_SPEED) | (1u << ROW_X) | (1u << ROW_Y) |
                                    (1u << ROW_Z) | (1u << ROW_ACTION);

static const float kMinW = 130.0f;
static const float kMaxW = 360.0f;
static const float kDefaultX = 40.0f;
static const float kDefaultY = 90.0f;
static const float kDefaultW = 164.0f;
static const float kDefaultH = 153.0f;
static const float kBaseW = 164.0f;
static const float kRowStep = 13.0f;
static const float kPadding = 23.0f;
static const float kMinBaseH = 62.0f;
static const float kFirstRowY = 12.0f;
static const float kLabelX = 12.0f;
static const float kValueRightX = 152.0f;
static const float kFontSize = 9.3f;

static const ImU32 kLabelColor = IM_COL32(180, 190, 204, 230);
static const ImU32 kValueColor = IM_COL32(255, 255, 255, 255);
static const ImU32 kDimColor = IM_COL32(156, 164, 176, 200);

struct RowInfo {
    const char* label;
    const char* shortLabel;
    const char* key;
};

static const RowInfo kRows[ROW_COUNT] = {
    { "Time of Day",     "ToD:",             "timeOfDay" },
    { "Current Session", "Current Session:", "currentSession" },
    { "Angle",           "Angle:",           "angle" },
    { "Y-Angle",         "Y-Angle:",         "lookAngle" },
    { "Speed",           "Speed:",           "speed" },
    { "X Position",      "X:",               "x" },
    { "Y Position",      "Y:",               "y" },
    { "Z Position",      "Z:",               "z" },
    { "Action",          "Action:",          "action" },
    { "Frame",           "Frame:",           "frame" },
    { "System Date",     "Date:",            "date" },
    { "System Time",     "Clock:",           "clock" },
};

static Ui::WindowState s_win = { false, kDefaultX, kDefaultY, kDefaultW, kDefaultH };
static bool     s_settingsOpen = false;
static uint32_t s_visibleRows = kDefaultRows;
static uint8_t  s_order[ROW_COUNT] = {
    ROW_TIME_OF_DAY, ROW_CURRENT_SESSION, ROW_ANGLE, ROW_Y_ANGLE, ROW_SPEED, ROW_X,
    ROW_Y, ROW_Z, ROW_ACTION, ROW_FRAME, ROW_DATE, ROW_CLOCK,
};
static bool   s_applyPos = false, s_applySize = false;
static float  s_layoutK = 0.0f;
static OSTime s_sessionStart = 0;

static uint32_t bit(int row) { return 1u << row; }

static unsigned visibleCount(uint32_t rows)
{
    unsigned count = 0;
    for (rows &= kAllRows; rows; rows >>= 1)
        count += rows & 1u;
    return count;
}

static float baseHeight()
{
    const float h = kPadding + kRowStep * visibleCount(s_visibleRows);
    return h < kMinBaseH ? kMinBaseH : h;
}

static void applyState()
{
    if (s_win.w < kMinW) s_win.w = kMinW;
    if (s_win.w > kMaxW) s_win.w = kMaxW;
    s_win.h = s_win.w * baseHeight() / kBaseW;
    s_applyPos = s_applySize = true;
}

static void setVisibleRows(uint32_t rows)
{
    rows &= kAllRows;
    if (!rows || rows == s_visibleRows)
        return;
    const float oldH = s_win.h;
    s_visibleRows = rows;
    applyState();
    s_win.y -= s_win.h - oldH;
    Config::MarkDirty();
}

static void resetOrder()
{
    for (int i = 0; i < ROW_COUNT; ++i)
        s_order[i] = (uint8_t)i;
}

static int rowForKey(const char* key)
{
    if (!key)
        return -1;
    for (int i = 0; i < ROW_COUNT; ++i)
        if (strcmp(kRows[i].key, key) == 0)
            return i;
    return -1;
}

Ui::WindowState& State() { return s_win; }

void ResetToDefaults()
{
    s_settingsOpen = false;
    s_visibleRows = kDefaultRows;
    resetOrder();
    s_win = { false, kDefaultX, kDefaultY, kDefaultW, kDefaultH };
    applyState();
}

void OnApplicationStart() { s_sessionStart = 0; }

void DrawSettingsButton()
{
    Ui::Window::Button("Settings##GameInfo", &s_settingsOpen, "Game Info Settings");
}

void DrawSettingsWindow()
{
    if (!s_settingsOpen)
        return;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Game Info Settings", &s_settingsOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("Visible information and display order");
        ImGui::Separator();
        int moveFrom = -1, moveTo = -1;
        const unsigned count = visibleCount(s_visibleRows);
        for (int i = 0; i < ROW_COUNT; ++i) {
            const int row = s_order[i];
            ImGui::PushID(row);
            bool visible = (s_visibleRows & bit(row)) != 0;
            const bool locked = visible && count == 1;
            ImGui::BeginDisabled(locked);
            if (ImGui::Checkbox(kRows[row].label, &visible))
                setVisibleRows(visible ? s_visibleRows | bit(row) : s_visibleRows & ~bit(row));
            ImGui::EndDisabled();
            ImGui::SameLine(300.0f);
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = i; moveTo = i - 1; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == ROW_COUNT);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { moveFrom = i; moveTo = i + 1; }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (moveFrom >= 0) {
            const uint8_t row = s_order[moveFrom];
            s_order[moveFrom] = s_order[moveTo];
            s_order[moveTo] = row;
            Config::MarkDirty();
        }
    }
    ImGui::End();
}

static void aspectConstraint(ImGuiSizeCallbackData* data)
{
    const float ratio = *(const float*)data->UserData;
    float w = data->DesiredSize.x, h = data->DesiredSize.y;
    if (w / ratio > h) h = w / ratio; else w = h * ratio;
    data->DesiredSize = ImVec2(w, h);
}

static const char* formatRow(int row, char* buf, size_t n, const daAlink_c* link, OSTime now)
{
    switch (row) {
    case ROW_TIME_OF_DAY: {
        u8 h = 0, m = 0;
        if (!Z2GetGameClock(&h, &m))
            return "--:--";
        snprintf(buf, n, "%02u:%02u", (unsigned)h, (unsigned)m);
        return buf;
    }
    case ROW_CURRENT_SESSION: {
        const uint64_t sec = (uint64_t)(now - s_sessionStart) / (uint64_t)OSTimerClockSpeed;
        snprintf(buf, n, "%02llu:%02llu:%02llu", (unsigned long long)(sec / 3600),
                 (unsigned long long)((sec / 60) % 60), (unsigned long long)(sec % 60));
        return buf;
    }
    case ROW_FRAME:
        snprintf(buf, n, "%u", (unsigned)cCt_getFrameCount());
        return buf;
    case ROW_DATE: {
        OSCalendarTime ct;
        OSTicksToCalendarTime(now, &ct);
        snprintf(buf, n, "%04d-%02d-%02d", ct.tm_year, ct.tm_mon + 1, ct.tm_mday);
        return buf;
    }
    case ROW_CLOCK: {
        OSCalendarTime ct;
        OSTicksToCalendarTime(now, &ct);
        snprintf(buf, n, "%02d:%02d:%02d", ct.tm_hour, ct.tm_min, ct.tm_sec);
        return buf;
    }
    }

    if (!link)
        return "--";

    switch (row) {
    case ROW_ANGLE:
        snprintf(buf, n, "%u", (unsigned)(uint16_t)link->shape_angle.y);
        return buf;
    case ROW_Y_ANGLE:
        snprintf(buf, n, "%d", (int)daPy_getLookAngleY(link));
        return buf;
    case ROW_SPEED:
        snprintf(buf, n, "%.7f", (double)link->speedF);
        return buf;
    case ROW_X:
        snprintf(buf, n, "%.7f", (double)link->current.pos.x);
        return buf;
    case ROW_Y:
        snprintf(buf, n, "%.7f", (double)link->current.pos.y);
        return buf;
    case ROW_Z:
        snprintf(buf, n, "%.7f", (double)link->current.pos.z);
        return buf;
    case ROW_ACTION:
        snprintf(buf, n, "%u", (unsigned)daAlink_getProcID(link));
        return buf;
    }
    return "--";
}

static bool moved(float a, float b) { return a - b > 0.5f || b - a > 0.5f; }

void DrawWindow(bool menuActive)
{
    const OSTime now = OSGetTime();
    if (!s_sessionStart || now < s_sessionStart)
        s_sessionStart = now;
    if (!s_win.enabled)
        return;

    const float k = 1.0f / Renderer::UiScale();
    if (k != s_layoutK) {
        s_layoutK = k;
        s_applyPos = s_applySize = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float hBase = baseHeight(), aspect = kBaseW / hBase;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    const float background = Config::g_settings.overlayOpacity;

    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinW * k, kMinW * k / aspect),
                                        ImVec2(kMaxW * k, kMaxW * k / aspect),
                                        aspectConstraint, (void*)&aspect);
    ImGui::SetNextWindowPos(ImVec2(s_win.x * k, s_win.y * k),
                            s_applyPos ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s_win.w * k, s_win.h * k),
                             s_applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    s_applyPos = s_applySize = false;
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.5f * background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * k, style.FramePadding.y * k));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    const bool wasEnabled = s_win.enabled;
    const float oldX = s_win.x, oldY = s_win.y, oldW = s_win.w, oldH = s_win.h;
    const bool open = ImGui::Begin("Game Info", menuActive ? &s_win.enabled : nullptr, flags);
    ImGui::SetWindowFontScale(k);
    Renderer::MarkGameScreenOnly(ImGui::GetWindowDrawList());
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    s_win.x = wp.x / k; s_win.y = wp.y / k; s_win.w = ws.x / k; s_win.h = ws.y / k;

    if (open) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / kBaseW;
        if (avail.y / hBase < scale)
            scale = avail.y / hBase;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 origin(cursor.x + (avail.x - kBaseW * scale) * 0.5f,
                            cursor.y + (avail.y - hBase * scale) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float fontSize = kFontSize * scale;
        dl->AddRectFilled(origin, ImVec2(origin.x + kBaseW * scale, origin.y + hBase * scale),
                          IM_COL32(0, 0, 0, (int)(115.0f * background)), 6.0f * scale);

        const daAlink_c* link = static_cast<const daAlink_c*>(dComIfGp_getPlayer());
        const bool bold = Config::g_settings.boldLetters;
        float rowY = kFirstRowY;
        for (int i = 0; i < ROW_COUNT; ++i) {
            const int row = s_order[i];
            if (!(s_visibleRows & bit(row)))
                continue;
            const bool dim = !link && (kPlayerRows & bit(row));
            const ImVec2 lp(origin.x + kLabelX * scale, origin.y + rowY * scale);
            DrawText(dl, font, fontSize, lp, dim ? kDimColor : kLabelColor,
                     kRows[row].shortLabel, bold);
            char buf[48];
            const char* value = formatRow(row, buf, sizeof(buf), link, now);
            const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value);
            const ImVec2 vp(origin.x + kValueRightX * scale - size.x, lp.y);
            DrawText(dl, font, fontSize, vp, dim ? kDimColor : kValueColor, value, bold);
            rowY += kRowStep;
        }
        ImGui::Dummy(avail);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    if (menuActive && (wasEnabled != s_win.enabled || moved(oldX, s_win.x) ||
                       moved(oldY, s_win.y) || moved(oldW, s_win.w) || moved(oldH, s_win.h)))
        Config::MarkDirty();
}

static void loadOrder(const cJSON* list)
{
    if (!cJSON_IsArray(list))
        return;
    uint8_t order[ROW_COUNT];
    uint32_t seen = 0;
    int n = 0;
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, list) {
        const int row = cJSON_IsString(entry) ? rowForKey(entry->valuestring) : -1;
        if (row < 0 || (seen & bit(row)))
            continue;
        seen |= bit(row);
        order[n++] = (uint8_t)row;
    }
    for (int i = 0; i < ROW_COUNT; ++i)
        if (!(seen & bit(i)))
            order[n++] = (uint8_t)i;
    memcpy(s_order, order, sizeof(s_order));
}

void LoadConfig(const cJSON* root)
{
    Config::LoadWindow(root, "gameInfo", s_win, kMinW, kMaxW);
    const cJSON* rows = cJSON_GetObjectItemCaseSensitive(root, "gameInfoRows");
    if (cJSON_IsObject(rows)) {
        uint32_t visible = 0;
        for (int i = 0; i < ROW_COUNT; ++i) {
            bool on = (kDefaultRows & bit(i)) != 0;
            Config::ReadBool(rows, kRows[i].key, &on);
            if (on)
                visible |= bit(i);
        }
        if (visible)
            s_visibleRows = visible;
    }
    loadOrder(cJSON_GetObjectItemCaseSensitive(root, "gameInfoRowOrder"));
    applyState();
}

void SaveConfig(cJSON* root)
{
    Config::SaveWindow(root, "gameInfo", s_win);
    if (cJSON* rows = cJSON_AddObjectToObject(root, "gameInfoRows"))
        for (int i = 0; i < ROW_COUNT; ++i)
            Config::AddBool(rows, kRows[i].key, (s_visibleRows & bit(i)) != 0);
    if (cJSON* order = cJSON_AddArrayToObject(root, "gameInfoRowOrder"))
        for (int i = 0; i < ROW_COUNT; ++i)
            cJSON_AddItemToArray(order, cJSON_CreateString(kRows[s_order[i]].key));
}
}
}
