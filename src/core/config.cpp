#include "core/config.h"

#include "cheats/cheat_difficulty.h"
#include "cheats/cheat_equipment.h"
#include "cheats/cheat_minigames.h"
#include "cheats/cheat_movement.h"
#include "cheats/cheat_player.h"
#include "core/config_schema.h"
#include "core/hotkeys.h"
#include "core/logger.h"
#include "core/settings.h"
#include "core/storage.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "tools/flycam.h"
#include "tools/practice_timer.h"
#include "tools/roll_checker.h"
#include "ui/menu_nav.h"
#include "ui/quick_access.h"
#include "ui/window_state.h"

#include "cJSON.h"

#include <coreinit/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Config {
static const uint32_t kMaxConfigBytes = 128 * 1024;
static const int64_t  kSaveCoalesceMs = 1000;
static const float    kMaxWindowCoord = 4096.0f;

static bool    s_loaded = false;
static bool    s_dirty = false;
static int64_t s_dirtySinceMs = 0;
static char    s_error[128] = "";

static int64_t nowMs()
{
    return (int64_t)OSTicksToMilliseconds(OSGetTime());
}

bool IsLoaded()         { return s_loaded; }
const char* LastError() { return s_error; }

void OnApplicationStart()
{
    ResetToDefaults();
    s_loaded = false;
    s_dirty = false;
    s_dirtySinceMs = 0;
    s_error[0] = '\0';
}

static cJSON* item(const cJSON* root, const char* key)
{
    return root ? cJSON_GetObjectItemCaseSensitive(root, key) : nullptr;
}

bool ReadBool(const cJSON* root, const char* key, bool* out)
{
    cJSON* v = item(root, key);
    if (!cJSON_IsBool(v))
        return false;
    *out = cJSON_IsTrue(v) != 0;
    return true;
}

bool ReadFloat(const cJSON* root, const char* key, float* out, float lo, float hi)
{
    cJSON* v = item(root, key);
    if (!cJSON_IsNumber(v))
        return false;
    float f = (float)v->valuedouble;
    if (f < lo) f = lo;
    if (f > hi) f = hi;
    *out = f;
    return true;
}

bool ReadInt(const cJSON* root, const char* key, int* out, int lo, int hi)
{
    cJSON* v = item(root, key);
    if (!cJSON_IsNumber(v))
        return false;
    double d = v->valuedouble;
    if (d < (double)lo) d = (double)lo;
    if (d > (double)hi) d = (double)hi;
    *out = (int)d;
    return true;
}

bool ReadU32(const cJSON* root, const char* key, uint32_t* out)
{
    cJSON* v = item(root, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0.0)
        return false;
    *out = (uint32_t)v->valuedouble;
    return true;
}

bool ReadString(const cJSON* root, const char* key, char* out, size_t size)
{
    cJSON* v = item(root, key);
    if (!cJSON_IsString(v) || !v->valuestring || !out || size == 0)
        return false;
    snprintf(out, size, "%s", v->valuestring);
    return true;
}

void AddBool(cJSON* root, const char* key, bool value)
{
    cJSON_AddBoolToObject(root, key, value);
}

void AddNumber(cJSON* root, const char* key, double value)
{
    cJSON_AddNumberToObject(root, key, value);
}

void AddString(cJSON* root, const char* key, const char* value)
{
    cJSON_AddStringToObject(root, key, value ? value : "");
}

static void keyFor(char* out, size_t size, const char* prefix, const char* suffix)
{
    snprintf(out, size, "%s%s", prefix, suffix);
}

void LoadWindow(const cJSON* root, const char* prefix, Ui::WindowState& st,
                float minW, float maxW)
{
    char key[64];
    keyFor(key, sizeof(key), prefix, "Enabled");
    ReadBool(root, key, &st.enabled);
    keyFor(key, sizeof(key), prefix, "X");
    ReadFloat(root, key, &st.x, -kMaxWindowCoord, kMaxWindowCoord);
    keyFor(key, sizeof(key), prefix, "Y");
    ReadFloat(root, key, &st.y, -kMaxWindowCoord, kMaxWindowCoord);
    keyFor(key, sizeof(key), prefix, "W");
    ReadFloat(root, key, &st.w, minW, maxW);
}

void SaveWindow(cJSON* root, const char* prefix, const Ui::WindowState& st)
{
    char key[64];
    keyFor(key, sizeof(key), prefix, "Enabled");
    cJSON_AddBoolToObject(root, key, st.enabled);
    keyFor(key, sizeof(key), prefix, "X");
    cJSON_AddNumberToObject(root, key, (double)st.x);
    keyFor(key, sizeof(key), prefix, "Y");
    cJSON_AddNumberToObject(root, key, (double)st.y);
    keyFor(key, sizeof(key), prefix, "W");
    cJSON_AddNumberToObject(root, key, (double)st.w);
}

static void* fieldPtr(const Field& f)
{
    return (char*)&g_settings + f.offset;
}

static void loadSettings(const cJSON* root)
{
    for (unsigned i = 0; i < kSettingsSchemaCount; ++i) {
        const Field& f = kSettingsSchema[i];
        switch (f.type) {
        case FIELD_BOOL:  ReadBool (root, f.key, (bool*)fieldPtr(f)); break;
        case FIELD_FLOAT: ReadFloat(root, f.key, (float*)fieldPtr(f), f.lo, f.hi); break;
        case FIELD_U32:   ReadU32  (root, f.key, (uint32_t*)fieldPtr(f)); break;
        }
    }
}

static void saveSettings(cJSON* root)
{
    for (unsigned i = 0; i < kSettingsSchemaCount; ++i) {
        const Field& f = kSettingsSchema[i];
        switch (f.type) {
        case FIELD_BOOL:  AddBool(root, f.key, *(bool*)fieldPtr(f)); break;
        case FIELD_FLOAT: AddNumber(root, f.key, (double)*(float*)fieldPtr(f)); break;
        case FIELD_U32:   AddNumber(root, f.key, (double)*(uint32_t*)fieldPtr(f)); break;
        }
    }
}

static void loadHotkeys(const cJSON* root)
{
    for (int i = 0; i < Hotkeys::HOTKEY_COUNT; ++i) {
        const Hotkeys::Id id = (Hotkeys::Id)i;
        uint32_t combo = Hotkeys::Get(id);
        ReadU32(root, Hotkeys::ConfigKey(id), &combo);
        Hotkeys::Set(id, combo);
        char key[64];
        keyFor(key, sizeof(key), Hotkeys::ConfigKey(id), "Inclusive");
        bool inclusive = Hotkeys::IsInclusive(id);
        ReadBool(root, key, &inclusive);
        Hotkeys::SetInclusive(id, inclusive);
    }
}

static void saveHotkeys(cJSON* root)
{
    for (int i = 0; i < Hotkeys::HOTKEY_COUNT; ++i) {
        const Hotkeys::Id id = (Hotkeys::Id)i;
        AddNumber(root, Hotkeys::ConfigKey(id), (double)Hotkeys::Get(id));
        char key[64];
        keyFor(key, sizeof(key), Hotkeys::ConfigKey(id), "Inclusive");
        AddBool(root, key, Hotkeys::IsInclusive(id));
    }
}

static void loadNavBindings(const cJSON* root)
{
    for (int i = 0; i < Ui::Nav::ACTION_COUNT; ++i) {
        const Ui::Nav::Action action = (Ui::Nav::Action)i;
        uint32_t button = Ui::Nav::Binding(action);
        ReadU32(root, Ui::Nav::ConfigKey(action), &button);
        if (button == 0 || !Ui::Nav::Validate(action, button))
            Ui::Nav::SetBinding(action, button);
    }
}

static void saveNavBindings(cJSON* root)
{
    for (int i = 0; i < Ui::Nav::ACTION_COUNT; ++i) {
        const Ui::Nav::Action action = (Ui::Nav::Action)i;
        AddNumber(root, Ui::Nav::ConfigKey(action), (double)Ui::Nav::Binding(action));
    }
}

static void loadQuickAccess(const cJSON* root)
{
    cJSON* items = item(root, "quickAccessItems");
    if (!cJSON_IsArray(items))
        return;
    Ui::QuickAccess::ClearItems();
    cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, items) {
        if (cJSON_IsString(entry) && entry->valuestring)
            Ui::QuickAccess::AddItem(entry->valuestring);
    }
}

static void saveQuickAccess(cJSON* root)
{
    cJSON* items = cJSON_AddArrayToObject(root, "quickAccessItems");
    if (!items)
        return;
    for (int i = 0; i < Ui::QuickAccess::ItemCount(); ++i)
        cJSON_AddItemToArray(items, cJSON_CreateString(Ui::QuickAccess::ItemId(i)));
}

struct Section {
    void (*load)(const cJSON* root);
    void (*save)(cJSON* root);
};

static const Section kSections[] = {
    { loadSettings,    saveSettings },
    { loadHotkeys,     saveHotkeys },
    { loadNavBindings, saveNavBindings },
    { loadQuickAccess, saveQuickAccess },
    { Hud::GameInfo::LoadConfig, Hud::GameInfo::SaveConfig },
    { Hud::InputViewer::LoadConfig, Hud::InputViewer::SaveConfig },
    { Tools::RollChecker::LoadConfig, Tools::RollChecker::SaveConfig },
    { Tools::PracticeTimer::LoadConfig, Tools::PracticeTimer::SaveConfig },
    { Tools::FlyCam::LoadConfig, Tools::FlyCam::SaveConfig },
    { Cheats::Player::LoadConfig, Cheats::Player::SaveConfig },
    { Cheats::Equipment::LoadConfig, Cheats::Equipment::SaveConfig },
    { Cheats::Difficulty::LoadConfig, Cheats::Difficulty::SaveConfig },
    { Cheats::Minigames::LoadConfig, Cheats::Minigames::SaveConfig },
    { Cheats::Movement::LoadConfig, Cheats::Movement::SaveConfig },
};

static void applyJson(const char* text)
{
    cJSON* root = cJSON_Parse(text);
    if (!root) {
        const char* err = cJSON_GetErrorPtr();
        snprintf(s_error, sizeof(s_error), "parse error near '%.24s'", err ? err : "?");
        Logger::LogWarn("[config] %s, keeping defaults", s_error);
        return;
    }
    for (const Section& s : kSections)
        s.load(root);
    cJSON_Delete(root);
}

void Poll()
{
    if (!s_loaded) {
        char* text = nullptr;
        const Storage::ReadResult r = Storage::LoadConfig(&text, kMaxConfigBytes);
        if (r == Storage::READ_RETRY)
            return;

        if (r == Storage::READ_OK && text) {
            applyJson(text);
            Logger::Log("[config] loaded from %s", Storage::RootPath());
        } else if (r == Storage::READ_MISSING) {
            Logger::Log("[config] no config.json yet, using defaults");
            s_dirty = true;
            s_dirtySinceMs = nowMs();
        } else {
            snprintf(s_error, sizeof(s_error), "read failed (%d)", (int)r);
            Logger::LogWarn("[config] %s", s_error);
        }

        free(text);
        s_loaded = true;
        return;
    }

    if (s_dirty && (nowMs() - s_dirtySinceMs) >= kSaveCoalesceMs)
        Flush();
}

void MarkDirty()
{
    if (!s_dirty) {
        s_dirty = true;
        s_dirtySinceMs = nowMs();
    }
}

void Flush()
{
    if (!s_dirty)
        return;
    s_dirty = false;

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        snprintf(s_error, sizeof(s_error), "out of memory building JSON");
        return;
    }
    for (const Section& s : kSections)
        s.save(root);

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        snprintf(s_error, sizeof(s_error), "out of memory printing JSON");
        return;
    }

    if (!Storage::SaveConfig(text)) {
        snprintf(s_error, sizeof(s_error), "write failed");
        Logger::LogWarn("[config] %s", s_error);
    } else {
        s_error[0] = '\0';
    }
    free(text);
}
}
