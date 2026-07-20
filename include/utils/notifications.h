// notifications.h -- transient action notifications shared by tools and UI.
#pragma once

struct ImGuiIO;

namespace Notifications {

// Notifications are queued and displayed in order. Calls are expected from the
// present thread, which is where tools accept their actions.
void Show(const char* message);
void Showf(const char* format, ...);
void Draw(ImGuiIO& io);
void Clear();

} // namespace Notifications
