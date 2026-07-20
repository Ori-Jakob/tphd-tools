// notifications.cpp -- see notifications.h.
#include "notifications.h"

#include "imgui.h"
#include "overlay.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Notifications {

static const int kQueueCapacity = 6;
static const int kMessageSize = 192;
static const float kDisplaySeconds = 2.5f;
static const float kFadeSeconds = 0.5f;

struct Entry {
    char text[kMessageSize];
};

static Entry s_queue[kQueueCapacity] = {};
static int s_queueHead = 0;
static int s_queueCount = 0;
static char s_active[kMessageSize] = {};
static float s_remaining = 0.0f;

void Clear()
{
    s_queueHead = 0;
    s_queueCount = 0;
    s_active[0] = '\0';
    s_remaining = 0.0f;
}

void Show(const char* message)
{
    if (!ov::g_settings.actionNotifications || !message || !message[0])
        return;

    // If every waiting slot is occupied, discard the oldest waiting message so
    // the newest action is never silently missed. The currently visible toast
    // remains undisturbed.
    if (s_queueCount == kQueueCapacity) {
        s_queueHead = (s_queueHead + 1) % kQueueCapacity;
        --s_queueCount;
    }

    const int tail = (s_queueHead + s_queueCount) % kQueueCapacity;
    snprintf(s_queue[tail].text, sizeof(s_queue[tail].text), "%s", message);
    ++s_queueCount;
}

void Showf(const char* format, ...)
{
    if (!ov::g_settings.actionNotifications || !format)
        return;

    char message[kMessageSize];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    message[sizeof(message) - 1] = '\0';
    Show(message);
}

static bool activateNext()
{
    if (s_active[0] || s_queueCount <= 0)
        return s_active[0] != '\0';

    snprintf(s_active, sizeof(s_active), "%s", s_queue[s_queueHead].text);
    s_queue[s_queueHead].text[0] = '\0';
    s_queueHead = (s_queueHead + 1) % kQueueCapacity;
    --s_queueCount;
    s_remaining = kDisplaySeconds;
    return true;
}

void Draw(ImGuiIO& io)
{
    if (!ov::g_settings.actionNotifications) {
        Clear();
        return;
    }
    if (!activateNext())
        return;

    float alpha = s_remaining < kFadeSeconds
                      ? s_remaining / kFadeSeconds
                      : 1.0f;
    if (alpha < 0.0f)
        alpha = 0.0f;

    const ImVec4 green(64.0f / 255.0f, 207.0f / 255.0f,
                       142.0f / 255.0f, 1.0f);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float contentWidth = ImGui::CalcTextSize("Action").x +
                               style.ItemSpacing.x +
                               ImGui::CalcTextSize(s_active).x;
    const float maxWindowWidth = io.DisplaySize.x * 0.34f;
    float windowWidth = contentWidth + style.WindowPadding.x * 2.0f;
    if (windowWidth > maxWindowWidth)
        windowWidth = maxWindowWidth;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::SetNextWindowBgAlpha(0.82f * alpha);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - 24.0f, io.DisplaySize.y * 0.075f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(windowWidth, 0.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##tphd_action_toast", nullptr, flags)) {
        ImGui::TextColored(green, "Action");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", s_active);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleVar();

    s_remaining -= io.DeltaTime;
    if (s_remaining <= 0.0f) {
        s_active[0] = '\0';
        s_remaining = 0.0f;
    }
}

} // namespace Notifications
