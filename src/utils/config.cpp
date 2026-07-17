// config.cpp -- see config.h.
//
// Persists settings as JSON (cJSON) through the build-specific Storage backend.
// Load is synchronous on the calling thread and retries until storage is ready
// (so a customized hotkey is available before the user can open the menu).
// Saves are debounced and performed on a background thread so the render thread
// never blocks on I/O.
#include "config.h"
#include "overlay.h"
#include "link_position.h"
#include "tools/input_viewer.h"
#include "tools/save_state.h"
#include "tools/auto_splitter.h"
#include "tools/modern_camera.h"
#include "cheats/cheats.h"
#include "storage.h"
#include "logger.h"

#include "imgui.h"   // ImGui Save/LoadIniSettingsToMemory -- persist window geometry

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/messagequeue.h>

#include "cJSON.h"

using namespace ov;

namespace Config {

static const int   kMaxLoadTries  = 1200;   // ~40s @30Hz before giving up + defaults
static const int   kDebounceFrames = 24;    // ~0.8s after the last change before saving

// --- load-side FS (main thread) ---
static bool       s_fsReady   = false;
static bool       s_loadDone  = false;
static int        s_loadTries = 0;

// --- change detection / debounce ---
struct Snap {
    bool block;
    int mode;
    uint32_t hotkey;
    bool freeze;
    bool gameResetHotkey;
    int controllerPref;
    int renderTarget;
    float overlayOpacity;
    bool boldLetters;
    float windowAdjustDeadzone;
    bool saveStateOverridePosition;
    bool saveStateReloadLastHotkey;
    char saveStateLastLoadedFolder[64];
    char saveStateLastLoaded[64];
    bool inputViewer;
    float inputViewerOpacity;
    bool link;
    bool gameInfoRealDateTime;
    float px;
    float py;
    float linkW;
    float linkH;
    float inputPx;
    float inputPy;
    float inputW;
    float inputH;
    bool autoSplitter;
    bool autoSplitterAutoStart;
    bool autoSplitterRemoveLoads;
    float autoSplitterDeltaPreviewSeconds;
    bool autoSplitterInitialsWhenDeltaShown;
    char autoSplitterPath[160];
    bool modernCamera;
    float modernCameraFovScale;
    float modernCameraSensitivityX;
    float modernCameraSensitivityY;
    bool modernCameraGlobalFov;
    bool modernCameraHorse;
    bool modernCameraInvertX;
    bool modernCameraInvertY;
    uint32_t cheatsMask;   // bit i = cheat i enabled (for change detection)
};
static Snap s_lastSaved;
static bool s_dirty = false;
static int  s_debounce = 0;
static bool s_forceSync = false;

// --- background saver thread ---
static OSThread       s_thread;
static bool           s_threadStarted = false;
static OSMessageQueue s_queue;
static OSMessage      s_msgBuf[8];
static __attribute__((aligned(16))) uint8_t s_stack[16 * 1024];

static Snap gather()
{
    Snap s;
    s.block  = g_settings.blockEnabled;
    s.mode   = g_settings.blockMode;
    s.hotkey = g_settings.hotkey;
    s.freeze = g_settings.freezeOnMenu;
    s.gameResetHotkey = g_settings.gameResetHotkey;
    s.controllerPref = g_settings.controllerPref;
    s.renderTarget = g_settings.renderTarget;
    s.overlayOpacity = g_settings.overlayOpacity;
    s.boldLetters = g_settings.boldLetters;
    s.windowAdjustDeadzone = g_settings.windowAdjustDeadzone;
    s.saveStateOverridePosition = Tools::SaveState::IsPositionOverrideEnabled();
    s.saveStateReloadLastHotkey = Tools::SaveState::IsReloadLastHotkeyEnabled();
    strncpy(s.saveStateLastLoadedFolder, Tools::SaveState::GetLastLoadedStateFolder(),
            sizeof(s.saveStateLastLoadedFolder) - 1);
    s.saveStateLastLoadedFolder[sizeof(s.saveStateLastLoadedFolder) - 1] = '\0';
    strncpy(s.saveStateLastLoaded, Tools::SaveState::GetLastLoadedStateName(),
            sizeof(s.saveStateLastLoaded) - 1);
    s.saveStateLastLoaded[sizeof(s.saveStateLastLoaded) - 1] = '\0';
    s.inputViewer = Tools::InputViewer::IsEnabled();
    s.inputViewerOpacity = Tools::InputViewer::GetOpacity();
    s.link   = Debug::LinkPosition::IsEnabled();
    s.gameInfoRealDateTime = Debug::LinkPosition::IsRealDateTimeEnabled();
    Debug::LinkPosition::GetWindowPos(&s.px, &s.py);
    Debug::LinkPosition::GetWindowSize(&s.linkW, &s.linkH);
    Tools::InputViewer::GetWindowPos(&s.inputPx, &s.inputPy);
    Tools::InputViewer::GetWindowSize(&s.inputW, &s.inputH);
    s.autoSplitter = Tools::AutoSplitter::IsEnabled();
    s.autoSplitterAutoStart = Tools::AutoSplitter::IsAutoStartEnabled();
    s.autoSplitterRemoveLoads = Tools::AutoSplitter::IsLoadRemovalEnabled();
    s.autoSplitterDeltaPreviewSeconds = Tools::AutoSplitter::GetDeltaPreviewSeconds();
    s.autoSplitterInitialsWhenDeltaShown =
        Tools::AutoSplitter::IsInitialsWhenDeltaShownEnabled();
    strncpy(s.autoSplitterPath, Tools::AutoSplitter::GetSelectedPath(),
            sizeof(s.autoSplitterPath) - 1);
    s.autoSplitterPath[sizeof(s.autoSplitterPath) - 1] = '\0';
    s.modernCamera = Tools::ModernCamera::IsEnabled();
    s.modernCameraFovScale = Tools::ModernCamera::GetFovScale();
    s.modernCameraSensitivityX = Tools::ModernCamera::GetSensitivityX();
    s.modernCameraSensitivityY = Tools::ModernCamera::GetSensitivityY();
    s.modernCameraGlobalFov = Tools::ModernCamera::IsGlobalFovEnabled();
    s.modernCameraHorse = Tools::ModernCamera::IsHorseEnabled();
    s.modernCameraInvertX = Tools::ModernCamera::IsInvertXEnabled();
    s.modernCameraInvertY = Tools::ModernCamera::IsInvertYEnabled();
    s.cheatsMask = 0;
    for (int i = 0; i < Cheats::Count() && i < 32; ++i)
        if (Cheats::IsEnabled(i))
            s.cheatsMask |= (1u << i);
    return s;
}

static bool snapEqual(const Snap& a, const Snap& b)
{
    return a.block == b.block && a.mode == b.mode && a.hotkey == b.hotkey &&
           a.freeze == b.freeze && a.gameResetHotkey == b.gameResetHotkey &&
           a.controllerPref == b.controllerPref && a.renderTarget == b.renderTarget &&
           a.overlayOpacity == b.overlayOpacity &&
           a.boldLetters == b.boldLetters &&
           a.windowAdjustDeadzone == b.windowAdjustDeadzone &&
           a.saveStateOverridePosition == b.saveStateOverridePosition &&
           a.saveStateReloadLastHotkey == b.saveStateReloadLastHotkey &&
           strcmp(a.saveStateLastLoadedFolder, b.saveStateLastLoadedFolder) == 0 &&
           strcmp(a.saveStateLastLoaded, b.saveStateLastLoaded) == 0 &&
           a.inputViewer == b.inputViewer &&
           a.inputViewerOpacity == b.inputViewerOpacity && a.link == b.link &&
           a.gameInfoRealDateTime == b.gameInfoRealDateTime && a.px == b.px &&
           a.py == b.py && a.linkW == b.linkW && a.linkH == b.linkH &&
           a.inputPx == b.inputPx && a.inputPy == b.inputPy && a.inputW == b.inputW &&
           a.inputH == b.inputH && a.autoSplitter == b.autoSplitter &&
           a.autoSplitterAutoStart == b.autoSplitterAutoStart &&
           a.autoSplitterRemoveLoads == b.autoSplitterRemoveLoads &&
           a.autoSplitterDeltaPreviewSeconds == b.autoSplitterDeltaPreviewSeconds &&
           a.autoSplitterInitialsWhenDeltaShown == b.autoSplitterInitialsWhenDeltaShown &&
           strcmp(a.autoSplitterPath, b.autoSplitterPath) == 0 &&
           a.modernCamera == b.modernCamera &&
           a.modernCameraFovScale == b.modernCameraFovScale &&
           a.modernCameraSensitivityX == b.modernCameraSensitivityX &&
           a.modernCameraSensitivityY == b.modernCameraSensitivityY &&
           a.modernCameraGlobalFov == b.modernCameraGlobalFov &&
           a.modernCameraHorse == b.modernCameraHorse &&
           a.modernCameraInvertX == b.modernCameraInvertX &&
           a.modernCameraInvertY == b.modernCameraInvertY &&
           a.cheatsMask == b.cheatsMask;
}

static bool ensureFs()
{
    if (s_fsReady)
        return true;
    if (!Storage::EnsureReady())
        return false;
    s_fsReady = true;
    return true;
}

// ---- (de)serialization ----
static char* serialize()
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return nullptr;
    float px, py;
    Debug::LinkPosition::GetWindowPos(&px, &py);
    cJSON_AddBoolToObject(root, "blockEnabled", g_settings.blockEnabled);
    cJSON_AddNumberToObject(root, "blockMode", g_settings.blockMode);
    cJSON_AddNumberToObject(root, "hotkey", (double)g_settings.hotkey);
    cJSON_AddBoolToObject(root, "freezeOnMenu", g_settings.freezeOnMenu);
    cJSON_AddBoolToObject(root, "gameResetHotkey", g_settings.gameResetHotkey);
    cJSON_AddNumberToObject(root, "controllerPref", g_settings.controllerPref);
    cJSON_AddNumberToObject(root, "renderTarget", g_settings.renderTarget);
    cJSON_AddNumberToObject(root, "overlayOpacity", g_settings.overlayOpacity);
    cJSON_AddBoolToObject(root, "boldLetters", g_settings.boldLetters);
    cJSON_AddNumberToObject(root, "windowAdjustDeadzone", g_settings.windowAdjustDeadzone);
    cJSON_AddBoolToObject(root, "saveStateOverridePosition",
                          Tools::SaveState::IsPositionOverrideEnabled());
    cJSON_AddBoolToObject(root, "saveStateReloadLastHotkey",
                          Tools::SaveState::IsReloadLastHotkeyEnabled());
    cJSON_AddStringToObject(root, "saveStateLastLoadedFolder",
                            Tools::SaveState::GetLastLoadedStateFolder());
    cJSON_AddStringToObject(root, "saveStateLastLoaded",
                            Tools::SaveState::GetLastLoadedStateName());
    cJSON_AddBoolToObject(root, "inputViewer", Tools::InputViewer::IsEnabled());
    cJSON_AddNumberToObject(root, "inputViewerOpacity", Tools::InputViewer::GetOpacity());
    cJSON_AddBoolToObject(root, "gameInfo", Debug::LinkPosition::IsEnabled());
    cJSON_AddBoolToObject(root, "gameInfoRealDateTime",
                          Debug::LinkPosition::IsRealDateTimeEnabled());
    cJSON_AddNumberToObject(root, "gameInfoPosX", px);
    cJSON_AddNumberToObject(root, "gameInfoPosY", py);
    Debug::LinkPosition::GetWindowSize(&px, &py);
    cJSON_AddNumberToObject(root, "gameInfoWidth", px);
    cJSON_AddNumberToObject(root, "gameInfoHeight", py);
    Tools::InputViewer::GetWindowPos(&px, &py);
    cJSON_AddNumberToObject(root, "inputViewerPosX", px);
    cJSON_AddNumberToObject(root, "inputViewerPosY", py);
    Tools::InputViewer::GetWindowSize(&px, &py);
    cJSON_AddNumberToObject(root, "inputViewerWidth", px);
    cJSON_AddNumberToObject(root, "inputViewerHeight", py);
    cJSON_AddBoolToObject(root, "autoSplitter", Tools::AutoSplitter::IsEnabled());
    cJSON_AddBoolToObject(root, "autoSplitterAutoStart",
                          Tools::AutoSplitter::IsAutoStartEnabled());
    cJSON_AddBoolToObject(root, "autoSplitterRemoveLoads",
                          Tools::AutoSplitter::IsLoadRemovalEnabled());
    cJSON_AddNumberToObject(root, "autoSplitterDeltaPreviewSeconds",
                            Tools::AutoSplitter::GetDeltaPreviewSeconds());
    cJSON_AddBoolToObject(root, "autoSplitterInitialsWhenDeltaShown",
                          Tools::AutoSplitter::IsInitialsWhenDeltaShownEnabled());
    cJSON_AddStringToObject(root, "autoSplitterPath",
                            Tools::AutoSplitter::GetSelectedPath());
    cJSON_AddBoolToObject(root, "modernCamera", Tools::ModernCamera::IsEnabled());
    cJSON_AddNumberToObject(root, "modernCameraFovScale",
                            Tools::ModernCamera::GetFovScale());
    cJSON_AddNumberToObject(root, "modernCameraSensitivityX",
                            Tools::ModernCamera::GetSensitivityX());
    cJSON_AddNumberToObject(root, "modernCameraSensitivityY",
                            Tools::ModernCamera::GetSensitivityY());
    cJSON_AddBoolToObject(root, "modernCameraGlobalFov",
                          Tools::ModernCamera::IsGlobalFovEnabled());
    cJSON_AddBoolToObject(root, "modernCameraHorse",
                          Tools::ModernCamera::IsHorseEnabled());
    cJSON_AddBoolToObject(root, "modernCameraInvertX",
                          Tools::ModernCamera::IsInvertXEnabled());
    cJSON_AddBoolToObject(root, "modernCameraInvertY",
                          Tools::ModernCamera::IsInvertYEnabled());
    // Enabled cheats, keyed by name so the file survives registry reordering.
    cJSON* cheats = cJSON_AddObjectToObject(root, "cheats");
    if (cheats)
        for (int i = 0; i < Cheats::Count(); ++i)
            cJSON_AddBoolToObject(cheats, Cheats::Name(i), Cheats::IsEnabled(i));
    // Window geometry (pos/size of the tool windows) -- ImGui's in-memory ini.
    const char* ini = ImGui::SaveIniSettingsToMemory(nullptr);
    cJSON_AddStringToObject(root, "imguiIni", ini ? ini : "");
    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

static void apply(const char* text)
{
    cJSON* root = cJSON_Parse(text);
    if (!root) {
        Logger::Log("[tphd_tools] config: JSON parse failed");
        return;
    }
    cJSON* it;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "blockEnabled")) && cJSON_IsBool(it))
        g_settings.blockEnabled = cJSON_IsTrue(it);
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "blockMode")) && cJSON_IsNumber(it))
        g_settings.blockMode = it->valueint;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "hotkey")) && cJSON_IsNumber(it))
        g_settings.hotkey = (uint32_t)it->valuedouble;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "freezeOnMenu")) && cJSON_IsBool(it))
        g_settings.freezeOnMenu = cJSON_IsTrue(it);
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "gameResetHotkey")) && cJSON_IsBool(it))
        g_settings.gameResetHotkey = cJSON_IsTrue(it);
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "controllerPref")) && cJSON_IsNumber(it))
        g_settings.controllerPref = it->valueint;
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "renderTarget")) &&
        cJSON_IsNumber(it) && it->valueint >= RENDER_TV &&
        it->valueint <= RENDER_BOTH) {
        g_settings.renderTarget = it->valueint;
    } else {
        g_settings.renderTarget = RENDER_TV;
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "overlayOpacity")) && cJSON_IsNumber(it)) {
        float opacity = (float)it->valuedouble;
        if (opacity < 0.0f)
            opacity = 0.0f;
        if (opacity > 1.0f)
            opacity = 1.0f;
        g_settings.overlayOpacity = opacity;
    } else {
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "boldLetters")) && cJSON_IsBool(it))
        g_settings.boldLetters = cJSON_IsTrue(it);
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "windowAdjustDeadzone")) &&
        cJSON_IsNumber(it)) {
        float dz = (float)it->valuedouble;
        if (dz < 0.0f)
            dz = 0.0f;
        if (dz > 0.9f)
            dz = 0.9f;
        g_settings.windowAdjustDeadzone = dz;
    } else {
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "saveStateReloadLastHotkey")) &&
        cJSON_IsBool(it)) {
        Tools::SaveState::SetReloadLastHotkeyEnabled(cJSON_IsTrue(it));
    } else {
        Tools::SaveState::SetReloadLastHotkeyEnabled(false);
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "saveStateLastLoadedFolder")) &&
        cJSON_IsString(it) && it->valuestring) {
        Tools::SaveState::SetLastLoadedStateFolder(it->valuestring);
    } else {
        Tools::SaveState::SetLastLoadedStateFolder("");
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "saveStateLastLoaded")) &&
        cJSON_IsString(it) && it->valuestring) {
        Tools::SaveState::SetLastLoadedStateName(it->valuestring);
    } else {
        Tools::SaveState::SetLastLoadedStateName("");
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "saveStateOverridePosition")) &&
        cJSON_IsBool(it)) {
        Tools::SaveState::SetPositionOverrideEnabled(cJSON_IsTrue(it));
    } else {
        s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "inputViewer")) && cJSON_IsBool(it))
        Tools::InputViewer::SetEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "inputViewerOpacity")) &&
        cJSON_IsNumber(it)) {
        Tools::InputViewer::SetOpacity((float)it->valuedouble);
    } else {
        Tools::InputViewer::SetOpacity(1.0f);
        s_forceSync = true;
    }
    it = cJSON_GetObjectItemCaseSensitive(root, "gameInfo");
    if (!it)
        it = cJSON_GetObjectItemCaseSensitive(root, "linkPosition");
    if (it && cJSON_IsBool(it))
        Debug::LinkPosition::SetEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "gameInfoRealDateTime")) && cJSON_IsBool(it))
        Debug::LinkPosition::SetRealDateTimeEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    {
        cJSON* jx = cJSON_GetObjectItemCaseSensitive(root, "gameInfoPosX");
        cJSON* jy = cJSON_GetObjectItemCaseSensitive(root, "gameInfoPosY");
        if (!jx)
            jx = cJSON_GetObjectItemCaseSensitive(root, "linkPosX");
        if (!jy)
            jy = cJSON_GetObjectItemCaseSensitive(root, "linkPosY");
        if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy))
            Debug::LinkPosition::SetWindowPos((float)jx->valuedouble, (float)jy->valuedouble);
        else
            s_forceSync = true;
    }
    {
        cJSON* jw = cJSON_GetObjectItemCaseSensitive(root, "gameInfoWidth");
        cJSON* jh = cJSON_GetObjectItemCaseSensitive(root, "gameInfoHeight");
        if (!jw)
            jw = cJSON_GetObjectItemCaseSensitive(root, "linkPositionWidth");
        if (!jh)
            jh = cJSON_GetObjectItemCaseSensitive(root, "linkPositionHeight");
        if (cJSON_IsNumber(jw) && cJSON_IsNumber(jh))
            Debug::LinkPosition::SetWindowSize((float)jw->valuedouble, (float)jh->valuedouble);
        else
            s_forceSync = true;
    }
    {
        cJSON* jx = cJSON_GetObjectItemCaseSensitive(root, "inputViewerPosX");
        cJSON* jy = cJSON_GetObjectItemCaseSensitive(root, "inputViewerPosY");
        if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy))
            Tools::InputViewer::SetWindowPos((float)jx->valuedouble, (float)jy->valuedouble);
    }
    {
        cJSON* jw = cJSON_GetObjectItemCaseSensitive(root, "inputViewerWidth");
        cJSON* jh = cJSON_GetObjectItemCaseSensitive(root, "inputViewerHeight");
        if (cJSON_IsNumber(jw) && cJSON_IsNumber(jh))
            Tools::InputViewer::SetWindowSize((float)jw->valuedouble, (float)jh->valuedouble);
        else
            s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitter")) &&
        cJSON_IsBool(it))
        Tools::AutoSplitter::SetEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitterAutoStart")) &&
        cJSON_IsBool(it))
        Tools::AutoSplitter::SetAutoStartEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitterRemoveLoads")) &&
        cJSON_IsBool(it))
        Tools::AutoSplitter::SetLoadRemovalEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitterDeltaPreviewSeconds")) &&
        cJSON_IsNumber(it))
        Tools::AutoSplitter::SetDeltaPreviewSeconds((float)it->valuedouble);
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitterInitialsWhenDeltaShown")) &&
        cJSON_IsBool(it))
        Tools::AutoSplitter::SetInitialsWhenDeltaShownEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "autoSplitterPath")) &&
        cJSON_IsString(it) && it->valuestring)
        Tools::AutoSplitter::SetSelectedPath(it->valuestring);
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCamera")) && cJSON_IsBool(it))
        Tools::ModernCamera::SetEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraFovScale")) &&
        cJSON_IsNumber(it))
        Tools::ModernCamera::SetFovScale((float)it->valuedouble);
    else
        s_forceSync = true;
    {
        // Older files stored one shared sensitivity; use it as the default
        // for both axes, then let the split keys override.
        cJSON* legacy = cJSON_GetObjectItemCaseSensitive(root, "modernCameraSensitivity");
        if (legacy && cJSON_IsNumber(legacy)) {
            Tools::ModernCamera::SetSensitivityX((float)legacy->valuedouble);
            Tools::ModernCamera::SetSensitivityY((float)legacy->valuedouble);
        }
        if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraSensitivityX")) &&
            cJSON_IsNumber(it))
            Tools::ModernCamera::SetSensitivityX((float)it->valuedouble);
        else
            s_forceSync = true;
        if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraSensitivityY")) &&
            cJSON_IsNumber(it))
            Tools::ModernCamera::SetSensitivityY((float)it->valuedouble);
        else
            s_forceSync = true;
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraGlobalFov")) &&
        cJSON_IsBool(it))
        Tools::ModernCamera::SetGlobalFovEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraHorse")) &&
        cJSON_IsBool(it))
        Tools::ModernCamera::SetHorseEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraInvertX")) &&
        cJSON_IsBool(it))
        Tools::ModernCamera::SetInvertXEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "modernCameraInvertY")) &&
        cJSON_IsBool(it))
        Tools::ModernCamera::SetInvertYEnabled(cJSON_IsTrue(it));
    else
        s_forceSync = true;
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "cheats")) && cJSON_IsObject(it)) {
        for (int i = 0; i < Cheats::Count(); ++i) {
            cJSON* c = cJSON_GetObjectItemCaseSensitive(it, Cheats::Name(i));
            if (c && cJSON_IsBool(c))
                Cheats::SetEnabled(i, cJSON_IsTrue(c));
        }
    }
    if ((it = cJSON_GetObjectItemCaseSensitive(root, "imguiIni")) && cJSON_IsString(it) &&
        it->valuestring && it->valuestring[0]) {
        ImGui::LoadIniSettingsFromMemory(it->valuestring, strlen(it->valuestring));
    }
    cJSON_Delete(root);
}

// ---- background saver ----
static int saveWorker(int argc, const char** argv)
{
    (void)argc; (void)argv;

    for (;;) {
        OSMessage msg;
        OSReceiveMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        char* text = (char*)msg.message;
        if (!text)
            continue;

        // Saves happen often (every settings tweak) -- don't log success, only the
        // (rare, actionable) failure.
        if (!Storage::SaveConfig(text))
            Logger::LogError("[tphd_tools] config save failed");
        cJSON_free(text);
    }
    return 0;   // not reached
}

static void startWorker()
{
    if (s_threadStarted)
        return;
    OSInitMessageQueue(&s_queue, s_msgBuf, (int32_t)(sizeof(s_msgBuf) / sizeof(s_msgBuf[0])));
    void* stackTop = s_stack + sizeof(s_stack);
    if (!OSCreateThread(&s_thread, saveWorker, 0, nullptr, stackTop, sizeof(s_stack), 16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        Logger::Log("[tphd_tools] config: worker thread create failed");
        return;
    }
    OSSetThreadName(&s_thread, "tphd_tools_saver");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
}

// Serialize current settings on this thread, hand the string to the saver thread.
static void requestSave()
{
    startWorker();
    if (!s_threadStarted)
        return;
    char* text = serialize();
    if (!text)
        return;
    OSMessage msg;
    msg.message = text;
    msg.args[0] = msg.args[1] = msg.args[2] = 0;
    if (!OSSendMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        // Queue full (saver behind) -- drop this write; a later change will save.
        cJSON_free(text);
    }
}

void Load()
{
    if (s_loadDone)
        return;
    if (!ensureFs())
        return;   // retry next frame

    char* buf = nullptr;
    Storage::ReadResult r = Storage::LoadConfig(&buf, 0x10000);
    if (r == Storage::READ_OK) {
        if (buf) {
            apply(buf);
            free(buf);
            Logger::Log("[tphd_tools] config loaded");
        }
        s_loadDone = true;
    } else if (r == Storage::READ_MISSING) {
        Logger::Log("[tphd_tools] config: no file yet -- using defaults");
        s_forceSync = true;
        s_loadDone = true;        // save mount is up, just no file: first run
    } else if (++s_loadTries >= kMaxLoadTries) {
        Logger::Log("[tphd_tools] config: storage not ready -- using defaults");
        s_loadDone = true;
    }

    if (s_loadDone) {
        s_lastSaved = gather();
        if (s_forceSync) {
            s_dirty = true;
            s_debounce = 1;
            s_forceSync = false;
        }
    }
}

void Update()
{
    if (!s_loadDone)
        return;   // don't save before load has settled

    Snap cur = gather();
    if (!snapEqual(cur, s_lastSaved)) {
        s_lastSaved = cur;            // remember latest; (re)start the debounce
        s_dirty     = true;
        s_debounce  = kDebounceFrames;
    }

    // ImGui flags this when a window was moved/resized -- persist the new geometry.
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;
        s_dirty    = true;
        s_debounce = kDebounceFrames;
    }

    if (s_dirty && --s_debounce <= 0) {
        s_dirty = false;
        requestSave();                // writes the current settings on the saver thread
    }
}

} // namespace Config
