// menu.cpp -- see menu.h.
//
// The UI is a workflow-oriented top menu bar shown while the overlay is active.
// Menu contents use plain widgets (Checkbox / RadioButton / Button), NOT
// MenuItems, so interacting never dismisses the menu.
//
// A true ImGui menu bar can normally only be *entered* with the keyboard Alt key
// (there is no default gamepad binding for the menu layer). So when the menu
// opens we focus the menu-bar window and synthesize a one-shot Alt tap, which
// drops gamepad navigation straight into the bar.
#include "menu.h"
#include "overlay.h"
#include "version.h"
#include "input.h"
#include "notifications.h"
#include "ui_hotkey.h"
#include "link_position.h"
#include "debug_save.h"
#include "tools/warp.h"
#include "tools/save_state.h"
#include "tools/save_load_coords.h"
#ifdef TPHD_TOOLS_EXPERIMENTAL
#include "tools/boss_practice.h"
#include "tools/auto_splitter.h"
#endif
#include "tools/flycam.h"
#include "tools/modern_camera.h"
#include "tools/input_viewer.h"
#include "tools/link_position_editor.h"
#include "cheats/cheats.h"
#include "cheats/inventory_editor.h"
#include "cheats/world_editor.h"
#include "game/game.h"          // dPad_* controller detection (Settings tab)

#include "imgui.h"
#include "imgui_internal.h"     // GImGui->NavWindow -- move the focused window

#include <stdio.h>
#include <string.h>

using namespace ov;

namespace Menu {

// Load toast: fully shown for (TOTAL - FADE) seconds, then fades over FADE.
static const float TOAST_TOTAL = 3.0f;
static const float TOAST_FADE  = 1.0f;
static float s_toast = 0.0f;     // seconds remaining; 0 = inactive
static int   s_navKick = 0;      // frames left to synthesize the Alt menu-enter
static bool  s_hotkeysWindow = false;  // Settings -> Rebind Hotkeys popup window
static bool  s_openConflictPopup = false;

void OnLoaded()
{
    s_toast = TOAST_TOTAL;
}

void Toggle()
{
    g_menuVisible = !g_menuVisible;
    s_toast = 0.0f;              // dismiss the toast once the user opens the menu
    if (g_menuVisible) {
        s_navKick = 2;          // Alt down (frame 2) then up (frame 1) -> enter menu layer
    } else {
        // Drop mid-rebind state so a held combo doesn't commit after the menu closes.
        if (Input::IsCapturingHotkey() || Input::IsHotkeyConflictPending())
            Input::CancelHotkeyCapture();
        s_hotkeysWindow = false;
        s_openConflictPopup = false;
    }
}

static void DrawToast(ImGuiIO& io)
{
    if (s_toast <= 0.0f)
        return;

    float alpha = (s_toast < TOAST_FADE) ? (s_toast / TOAST_FADE) : 1.0f;

    char hk[64];
    Input::HotkeyToString(g_settings.hotkey, hk, sizeof(hk));

    // Colors: name and hotkey buttons use the input viewer's held-button green;
    // the version retains its pressed-button gold.
    const ImVec4 kGreen(64.0f / 255.0f, 207.0f / 255.0f, 142.0f / 255.0f, 1.0f);
    const ImVec4 kGold (255.0f / 255.0f, 214.0f / 255.0f, 92.0f / 255.0f, 1.0f);
    const char* name     = "TPHD Tools";
    const char* hintPre  = "Press  ";
    const char* hintPost = "  to open the menu.";

    // Pre-measure both lines and give the window a deterministic size: a fraction of
    // the screen, but never narrower than the widest line. A fixed width means the
    // per-line centering below is correct on the very first frame -- AlwaysAutoResize
    // only settles the width after a frame, which is what nudged the title off-center.
    float wSpace = ImGui::CalcTextSize(" ").x;
    float titleW = ImGui::CalcTextSize(name).x + wSpace + ImGui::CalcTextSize(TPHD_TOOLS_VERSION).x;
    float hintW  = ImGui::CalcTextSize(hintPre).x + ImGui::CalcTextSize(hk).x +
                   ImGui::CalcTextSize(hintPost).x;
    float contentW = (titleW > hintW) ? titleW : hintW;
    float winW = io.DisplaySize.x * 0.22f;
    float minW = contentW + ImGui::GetStyle().WindowPadding.x * 2.0f;
    if (winW < minW)
        winW = minW;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::SetNextWindowBgAlpha(0.75f * alpha);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.06f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(winW, 0.0f), ImGuiCond_Always);   // fixed width, auto height
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##tphd_toast", nullptr, flags)) {
        // Title line: "TPHD Tools" (green) + version (gold), centered as one line.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
        ImGui::TextColored(kGreen, "%s", name);
        ImGui::SameLine(0.0f, wSpace);
        ImGui::TextColored(kGold, "%s", TPHD_TOOLS_VERSION);

        // Hint line: button labels are green while combo separators stay normal.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (ImGui::GetContentRegionAvail().x - hintW) * 0.5f);
        UiHotkey::DrawText(g_settings.hotkey, hintPre, hintPost);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    s_toast -= io.DeltaTime;
}

static void DrawSettingsMenu()
{
    // Plain widgets (not MenuItems) so interacting never dismisses the menu.
    ImGui::SeparatorText("Menu Behavior");
    ImGui::Checkbox("Block game input", &g_settings.blockEnabled);
    if (g_settings.blockEnabled) {
        ImGui::Indent();
        ImGui::RadioButton("All inputs", &g_settings.blockMode, BLOCK_ALL);
        ImGui::RadioButton("GamePad only", &g_settings.blockMode, BLOCK_GAMEPAD);
        ImGui::RadioButton("Pro Controller only", &g_settings.blockMode, BLOCK_PRO);
        ImGui::Unindent();
    }

    ImGui::Checkbox("Freeze game while menu open", &g_settings.freezeOnMenu);
    ImGui::Checkbox("Action notifications", &g_settings.actionNotifications);
    ImGui::Checkbox("Game reset hotkey", &g_settings.gameResetHotkey);
    if (g_settings.gameResetHotkey) {
        ImGui::Indent();
        UiHotkey::Draw(g_settings.gameResetCombo, true);
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Controllers");

    // Controller finalized when a save / debug save is loaded from the title screen
    // (before the game's own controller-select). Auto uses the Pro Controller when
    // one is connected, otherwise the GamePad.
    ImGui::TextUnformatted("Load-screen controller");
    ImGui::Indent();
    // Changing the option also applies it to the live controller right away (not
    // just for the next title-screen load) -- but only on an actual change.
    int prevControllerPref = g_settings.controllerPref;
    ImGui::RadioButton("Auto##ctrl", &g_settings.controllerPref, CTRL_AUTO);
    ImGui::SameLine();
    ImGui::RadioButton("GamePad##ctrl", &g_settings.controllerPref, CTRL_GAMEPAD);
    ImGui::SameLine();
    ImGui::RadioButton("Pro##ctrl", &g_settings.controllerPref, CTRL_PRO);
    if (g_settings.controllerPref != prevControllerPref)
        dPad_setController(g_settings.controllerPref);
    {
        bool gp  = dPad_isGamePadConnected();
        bool pro = dPad_isProConnected();
        const char* det = (gp && pro) ? "GamePad + Pro" : pro ? "Pro" : gp ? "GamePad" : "none";
        ImGui::TextDisabled("Connected: %s", det);
    }
    ImGui::Unindent();

    ImGui::SeparatorText("Display & Windows");

    ImGui::TextUnformatted("Draw overlay on");
    ImGui::Indent();
    ImGui::RadioButton("TV##target", &g_settings.renderTarget, RENDER_TV);
    ImGui::SameLine();
    ImGui::RadioButton("GamePad##target", &g_settings.renderTarget, RENDER_GAMEPAD);
    ImGui::SameLine();
    ImGui::RadioButton("Both##target", &g_settings.renderTarget, RENDER_BOTH);
    ImGui::Unindent();

    int overlayOpacityPercent = (int)(g_settings.overlayOpacity * 100.0f + 0.5f);
    if (ImGui::SliderInt("Overlay background opacity", &overlayOpacityPercent, 0, 100,
                         "%d%%")) {
        g_settings.overlayOpacity = overlayOpacityPercent / 100.0f;
    }

    ImGui::Checkbox("Bold letters", &g_settings.boldLetters);

    // Deadzone for the ZL + stick window move/resize (so a resting stick doesn't
    // drift the focused window).
    ImGui::SliderFloat("Window move/resize deadzone", &g_settings.windowAdjustDeadzone,
                       0.0f, 0.9f, "%.2f");
    if (g_settings.windowAdjustDeadzone < 0.0f)
        g_settings.windowAdjustDeadzone = 0.0f;
    if (g_settings.windowAdjustDeadzone > 0.9f)
        g_settings.windowAdjustDeadzone = 0.9f;

    ImGui::SeparatorText("Hotkeys");

    UiHotkey::DrawText(g_settings.hotkey, "Menu hotkey: ");
    if (ImGui::Button("Rebind Hotkeys"))
        s_hotkeysWindow = true;
}

struct HotkeyMenuEntry {
    const char* group;
    Input::HotkeyId id;
};

// Display order only. HotkeyId values and their persisted config fields remain
// unchanged so existing configurations continue to load without migration.
static const HotkeyMenuEntry kHotkeyMenuOrder[] = {
    { "General",  Input::HOTKEY_MENU },
    { nullptr,    Input::HOTKEY_GAME_RESET },
    { "Practice", Input::HOTKEY_SAVE_STATE_RELOAD },
    { nullptr,    Input::HOTKEY_SAVE_COORDINATES },
    { nullptr,    Input::HOTKEY_LOAD_COORDINATES },
    { "Camera",   Input::HOTKEY_FLY_CAM },
    { "Gameplay", Input::HOTKEY_QUICK_TRANSFORM },
    { nullptr,    Input::HOTKEY_MOON_JUMP },
    { nullptr,    Input::HOTKEY_REMOTE_BOMBS },
};
static_assert(sizeof(kHotkeyMenuOrder) / sizeof(kHotkeyMenuOrder[0]) ==
                  Input::HOTKEY_COUNT,
              "Every rebindable hotkey must appear in the grouped menu");

// Popup table of every rebindable feature hotkey. Capture uses the same press-
// then-release flow as the original menu-hotkey Rebind; exact collisions with
// another assigned hotkey open a confirmation before overwriting that slot.
static void DrawHotkeysWindow()
{
    if (!s_hotkeysWindow)
        return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
    bool open = ImGui::Begin("Rebind Hotkeys", &s_hotkeysWindow, ImGuiWindowFlags_NoCollapse);
    if (open) {
        ImGui::TextDisabled(
            "Press buttons, then release to set. Conflicts ask before overwriting.");
        ImGui::Separator();

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##hotkey_table", 3, flags)) {
            ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("Hotkey",  ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed,   90.0f);
            ImGui::TableHeadersRow();

            for (unsigned i = 0;
                 i < sizeof(kHotkeyMenuOrder) / sizeof(kHotkeyMenuOrder[0]);
                 ++i) {
                const HotkeyMenuEntry& entry = kHotkeyMenuOrder[i];
                Input::HotkeyId id = entry.id;
                if (entry.group) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", entry.group);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Input::HotkeyName(id));

                ImGui::TableNextColumn();
                bool capturingThis =
                    Input::IsCapturingHotkey() && Input::CapturingHotkeyId() == id;
                if (capturingThis) {
                    ImGui::TextDisabled("press buttons, release to set");
                } else {
                    UiHotkey::Draw(Input::GetHotkey(id));
                }

                ImGui::TableNextColumn();
                // Disable other Rebind buttons while capturing or resolving a conflict.
                bool busy = Input::IsCapturingHotkey() || Input::IsHotkeyConflictPending();
                ImGui::BeginDisabled(busy && !capturingThis);
                char label[32];
                if (capturingThis)
                    snprintf(label, sizeof(label), "Cancel##hk%d", (int)id);
                else
                    snprintf(label, sizeof(label), "Rebind##hk%d", (int)id);
                if (ImGui::Button(label)) {
                    if (capturingThis)
                        Input::CancelHotkeyCapture();
                    else
                        Input::BeginHotkeyCapture(id);
                }
                ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }

        // Capture finished onto a mask another feature already owns.
        if (Input::IsHotkeyConflictPending())
            s_openConflictPopup = true;
        if (s_openConflictPopup) {
            ImGui::OpenPopup("Hotkey conflict");
            s_openConflictPopup = false;
        }
        if (ImGui::BeginPopupModal("Hotkey conflict", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            char names[128];
            Input::HotkeyConflictNames(names, sizeof(names));
            UiHotkey::DrawText(Input::PendingHotkeyMask(), "\"",
                               "\" is already assigned to:");
            ImGui::TextWrapped("%s", names);
            ImGui::Spacing();
            ImGui::TextUnformatted("Overwrite? The other feature(s) will be cleared.");
            ImGui::Spacing();
            if (ImGui::Button("Overwrite", ImVec2(120, 0))) {
                Input::ConfirmHotkeyConflict();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                Input::CancelHotkeyConflict();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    // Window X closed this frame -- drop any in-progress rebind state.
    if (!s_hotkeysWindow) {
        if (Input::IsCapturingHotkey() || Input::IsHotkeyConflictPending())
            Input::CancelHotkeyCapture();
        s_openConflictPopup = false;
    }
}

static void DrawPracticeMenu()
{
    ImGui::SeparatorText("Save & Restore");
    Tools::SaveState::DrawMenuItem();
    Tools::SaveLoadCoords::DrawMenuItem();
    Debug::DebugSave::DrawMenuItem();

    ImGui::SeparatorText("Navigation");
    Tools::Warp::DrawMenuItem();
    Tools::LinkPositionEditor::DrawMenuItem();
}

static void DrawCameraHudMenu()
{
    ImGui::SeparatorText("Camera");
    Tools::FlyCam::DrawMenuItem();
    if (Tools::FlyCam::IsEnabled()) {
        ImGui::Indent();
        UiHotkey::DrawText(g_settings.flyCamCombo, nullptr, " to fly", true);
        ImGui::Unindent();
    }

    Tools::ModernCamera::DrawToggle();
    if (ImGui::BeginMenu("Modern Camera Settings")) {
        Tools::ModernCamera::DrawSettingsMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Advanced Diagnostics")) {
        Tools::ModernCamera::DrawDiagnosticsMenu();
        ImGui::EndMenu();
    }

    ImGui::SeparatorText("HUD");
    Debug::LinkPosition::DrawMenuItem();
    Tools::InputViewer::DrawMenuItem();
}

#ifdef TPHD_TOOLS_EXPERIMENTAL
static void DrawExperimentalMenu()
{
    ImGui::SeparatorText("Practice");
    Tools::BossPractice::DrawMenuItem();
    ImGui::SeparatorText("Timing & Splits");
    Tools::AutoSplitter::DrawMenuItem();
}
#endif

// A top-level tool window we manage (move/resize/clamp)? Skips child windows,
// popups/tooltips, the menu bar, and our non-interactive toasts.
static bool isManageableWindow(ImGuiWindow* w)
{
    if (!w || !w->Active || w->Hidden)
        return false;
    if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
                    ImGuiWindowFlags_Tooltip))
        return false;
    if (w->Name) {
        if (strcmp(w->Name, "##MainMenuBar") == 0) return false;
        if (strcmp(w->Name, "##tphd_toast") == 0)  return false;
        if (strcmp(w->Name, "##tphd_action_toast") == 0) return false;
    }
    return true;
}

// Clamp a window so its whole rect stays inside the viewport. If the window is
// bigger than the viewport (can't fit), pin its top-left to the corner.
static void clampWindowPos(ImGuiWindow* w, ImVec2 disp)
{
    ImVec2 pos = w->Pos;
    float maxX = disp.x - w->Size.x;
    float maxY = disp.y - w->Size.y;
    if (pos.x > maxX) pos.x = maxX;
    if (pos.y > maxY) pos.y = maxY;
    if (pos.x < 0.0f) pos.x = 0.0f;
    if (pos.y < 0.0f) pos.y = 0.0f;
    if (pos.x != w->Pos.x || pos.y != w->Pos.y)
        ImGui::SetWindowPos(w, pos, ImGuiCond_Always);
}

// While the menu is open, holding ZL and pushing a stick adjusts the gamepad-
// focused window: right stick moves it, left stick resizes it. The sticks are
// otherwise unused by ImGui nav, so they're free. Operates on the focused
// window's root (so a focused child drags/sizes its parent). The global clamp
// pass below keeps the result on-screen.
static void AdjustFocusedWindow(ImGuiIO& io)
{
    if (!g_menuVisible)
        return;
    Input::Snapshot snap;
    Input::GetSnapshot(&snap);
    if (!(snap.buttons & MB_ZL))
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* w = (g && g->NavWindow) ? g->NavWindow->RootWindow : nullptr;
    if (!isManageableWindow(w))
        return;

    // Radial deadzone: compare squared magnitudes against the configured stick
    // deadzone (a 0..1 magnitude in Settings), so a resting stick never drifts.
    const float dz         = g_settings.windowAdjustDeadzone;
    const float kDead      = dz * dz;
    const float kMoveSpeed = 40.0f;   // px/frame at full tilt
    const float kSizeSpeed = 25.0f;

    // Move with the right stick (stick up -> screen up).
    float rx = snap.rightX, ry = snap.rightY;
    if (rx * rx + ry * ry >= kDead) {
        ImGui::SetWindowPos(w, ImVec2(w->Pos.x + rx * kMoveSpeed,
                                      w->Pos.y - ry * kMoveSpeed), ImGuiCond_Always);
    }

    // Resize with the left stick (right widens, up grows taller). Auto-resize
    // windows compute their own size, so leave those alone.
    float lx = snap.leftX, ly = snap.leftY;
    if (!(w->Flags & ImGuiWindowFlags_AlwaysAutoResize) && lx * lx + ly * ly >= kDead) {
        const float kMinW = 120.0f, kMinH = 60.0f;
        ImVec2 size(w->Size.x + lx * kSizeSpeed, w->Size.y - ly * kSizeSpeed);
        if (size.x < kMinW) size.x = kMinW;
        if (size.y < kMinH) size.y = kMinH;
        if (size.x > io.DisplaySize.x) size.x = io.DisplaySize.x;   // never exceed screen
        if (size.y > io.DisplaySize.y) size.y = io.DisplaySize.y;
        ImGui::SetWindowSize(w, size, ImGuiCond_Always);
    }
}

// Y cycles gamepad focus through the open tool windows, then back to the menu
// bar (NOT the held-button "windowing" overlay -- a direct, one-press cycle). The
// windows are visited in their stable per-frame Begin order so the sequence
// doesn't jump as focusing reorders g->Windows. Slot 0 is the menu bar; wrapping
// to it re-enters the menu layer (the same Alt tap used when the menu opens).
static void CycleWindow()
{
    if (!g_menuVisible)
        return;
    Input::Snapshot snap;
    Input::GetSnapshot(&snap);
    if (!(snap.pressed & MB_Y))
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return;

    // Enumerate exactly the windows the X window-picker lists: the focus-order list
    // filtered by IsWindowNavFocusable() (top-level, nav-focusable, was active),
    // minus our own bars/overlays. This is the authoritative "focusable windows"
    // source -- iterating g->Windows ourselves missed some. Sorted by Begin order
    // for a stable cycle sequence.
    ImGuiWindow* list[32];
    int n = 0;
    for (int i = 0; i < g->WindowsFocusOrder.Size && n < 32; ++i) {
        ImGuiWindow* w = g->WindowsFocusOrder[i];
        if (!w || !ImGui::IsWindowNavFocusable(w))
            continue;
        if (w->Name && (strcmp(w->Name, "##MainMenuBar") == 0 ||
                        strcmp(w->Name, "##tphd_toast") == 0 ||
                        strcmp(w->Name, "##tphd_action_toast") == 0 ||
                        strcmp(w->Name, "###NavWindowingList") == 0))
            continue;
        list[n++] = w;
    }
    for (int i = 1; i < n; ++i) {            // insertion sort by Begin order
        ImGuiWindow* k = list[i];
        int j = i - 1;
        while (j >= 0 && list[j]->BeginOrderWithinContext > k->BeginOrderWithinContext) {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = k;
    }

    // Cycle position is remembered (s_cycleIdx: -1 = menu bar, else index into the
    // sorted list) and re-synced to live focus ONLY when it matches a listed window.
    // We deliberately do NOT snap back to -1 just because NavWindow is the menu bar:
    // the bar reclaims nav focus between presses, and resetting on it made Y bounce
    // forever between the bar and window 0.
    static int s_cycleIdx = -1;
    ImGuiWindow* cur = g->NavWindow ? g->NavWindow->RootWindow : nullptr;
    for (int i = 0; i < n; ++i)
        if (list[i] == cur) { s_cycleIdx = i; break; }
    if (s_cycleIdx >= n)
        s_cycleIdx = n - 1;                  // windows closed since last press

    // Advance: -1 (bar) -> 0 -> 1 -> ... -> n-1 -> -1.
    if (s_cycleIdx < 0)
        s_cycleIdx = (n > 0) ? 0 : -1;
    else if (s_cycleIdx >= n - 1)
        s_cycleIdx = -1;
    else
        s_cycleIdx += 1;

    if (s_cycleIdx < 0)
        s_navKick = 2;                       // re-enter the menu bar (Alt tap next frame)
    else
        ImGui::FocusWindow(list[s_cycleIdx], ImGuiFocusRequestFlags_RestoreFocusedChild);
}

// If the focused window has a tab bar, L / R cycle its tabs (previous / next).
// We find the tab bar that was submitted inside the focused window this frame and
// queue a focus to the adjacent tab.
static void CycleTabs()
{
    if (!g_menuVisible)
        return;
    Input::Snapshot snap;
    Input::GetSnapshot(&snap);
    int dir = ((snap.pressed & MB_R) ? 1 : 0) - ((snap.pressed & MB_L) ? 1 : 0);
    if (dir == 0)
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* w = (g && g->NavWindow) ? g->NavWindow->RootWindow : nullptr;
    if (!w)
        return;

    ImRect wr = w->Rect();
    ImGuiTabBar* bar = nullptr;
    for (int i = 0; i < g->TabBars.Map.Data.Size; ++i) {
        int idx = g->TabBars.Map.Data[i].val_i;
        if (idx == -1)
            continue;
        ImGuiTabBar* tb = g->TabBars.GetByIndex(idx);
        if (!tb || tb->CurrFrameVisible != g->FrameCount)   // not submitted this frame
            continue;
        if (wr.Contains(tb->BarRect.Min)) { bar = tb; break; }
    }
    if (!bar || bar->Tabs.Size < 2)
        return;

    int cur = 0;
    for (int i = 0; i < bar->Tabs.Size; ++i)
        if (bar->Tabs[i].ID == bar->SelectedTabId) { cur = i; break; }
    int next = (cur + dir + bar->Tabs.Size) % bar->Tabs.Size;
    ImGui::TabBarQueueFocus(bar, &bar->Tabs[next]);
}

// Keep every managed window fully inside the viewport, however it was moved or
// resized (gamepad above, or touch/mouse dragging). Runs every frame so a window
// can never be left straddling an edge.
static void ClampWindowsToViewport(ImGuiIO& io)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return;
    for (ImGuiWindow* w : g->Windows) {
        if (!isManageableWindow(w) || w->Size.x <= 0.0f || w->Size.y <= 0.0f)
            continue;
        clampWindowPos(w, io.DisplaySize);
    }
}

void Draw(ImGuiIO& io)
{
    DrawToast(io);

    if (g_menuVisible) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Practice")) {
                DrawPracticeMenu();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Gameplay")) {
                Cheats::DrawGameplayMenu();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Camera & HUD")) {
                DrawCameraHudMenu();
                ImGui::EndMenu();
            }
#ifdef TPHD_TOOLS_EXPERIMENTAL
            if (ImGui::BeginMenu("Experimental")) {
                DrawExperimentalMenu();
                ImGui::EndMenu();
            }
#endif
            if (ImGui::BeginMenu("Settings")) {
                DrawSettingsMenu();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Drop gamepad nav into the menu bar on open: focus the bar window and
        // tap Alt (down this frame, up next) to toggle into the menu layer.
        if (s_navKick > 0) {
            ImGui::SetWindowFocus("##MainMenuBar");
            io.AddKeyEvent(ImGuiMod_Alt, s_navKick > 1);
            s_navKick--;
        }
    }

    // Interactive tool panels (Warps, Save Loader, Debug Save Loader) only exist
    // while the menu bar is up -- they're not passive HUDs.
    if (g_menuVisible) {
        DrawHotkeysWindow();
        Tools::Warp::DrawWindow(true);
        Tools::SaveState::DrawWindow(true);
#ifdef TPHD_TOOLS_EXPERIMENTAL
        Tools::BossPractice::DrawWindow(true);
#endif
        Tools::LinkPositionEditor::DrawWindow(true);
        Debug::DebugSave::DrawWindow(true);
        Cheats::InventoryEditor::DrawWindow(true);
        Cheats::WorldEditor::DrawWindow(true);
    }

    // Passive HUD windows stay on-screen (locked, no input) when the menu closes.
    Tools::InputViewer::DrawWindow(g_menuVisible);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    Tools::AutoSplitter::DrawWindow(g_menuVisible);
#endif
    Debug::LinkPosition::DrawWindow(g_menuVisible);

    // Gamepad window management: Y cycles windows, L/R cycle the focused window's
    // tabs, ZL + sticks move/resize it, then clamp everything on-screen.
    CycleWindow();
    CycleTabs();
    AdjustFocusedWindow(io);
    ClampWindowsToViewport(io);

    // Action notifications are drawn last so tool windows cannot cover them.
    // They are separate from the centered initialization toast above.
    Notifications::Draw(io);
}

} // namespace Menu
