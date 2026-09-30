#include "tools/debug_saves.h"

#include "core/controller.h"
#include "core/logger.h"
#include "core/storage.h"
#include "tools/save_states.h"
#include "tools/warp_table.h"
#include "ui/ui_window.h"

#include "d/d_com_inf_game.h"
#include "d/d_save.h"

#include "imgui.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace Tools {
namespace DebugSaves {
enum { TAB_DEBUG = 0, TAB_QA, TAB_CUTSCENE, TAB_PERSONAL, TAB_COUNT };

struct TabDef {
    const char* title;
    const char* folder;
    const char* prefix;
    bool        personal;
};

static const TabDef kTabs[TAB_COUNT] = {
    { "Debug",    "fs:/vol/content/DebugSaves",         "",         false },
    { "QA",       "fs:/vol/content/DebugSaves/QASaves", "QASaves/", false },
    { "Cutscene", "fs:/vol/content/DebugSaves/CSSaves", "CSSaves/", false },
    { "Personal", "saves",                              "",         true  },
};

static const char     kWindowName[] = "Debug Save Loader";
static const int      kPathMax = 160;
static const int      kContentCapacity = 96;
static const int      kPersonalCapacity = 512;
static const int      kMaxScanDepth = 8;
// The game copies the name into a 32-byte buffer and builds the file path from it.
static const size_t   kEngineNameMax = 31;
// Either the raw serialized image or the full save file with its 8-byte trailer.
static const uint32_t kPersonalFileSize = 0x0E00u;
static const uint32_t kPeekSize =
    offsetof(dSv_info_c, mReturnPlace) + sizeof(dSv_player_return_place_c);

struct Entry {
    char        path[kPathMax];
    int         stemLength;
    char        stage[9];
    const char* place;
};

struct PersonalScan {
    char dir[kPathMax];
    int  depth;
};

static bool   s_open = false;
static Entry* s_entries = nullptr;
static int    s_count = 0;
static int    s_capacity = 0;
static int    s_listTab = -1;
static bool   s_scanFailed = false;
static char   s_status[96] = "";
static char   s_request[kPathMax] = "";
static bool   s_requestPersonal = false;
alignas(64) static uint8_t s_image[kPersonalFileSize];

static void setStatus(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static bool endsWithDat(const char* name)
{
    const size_t length = strlen(name);
    if (length <= 4 || name[length - 4] != '.')
        return false;
    return tolower((unsigned char)name[length - 3]) == 'd' &&
           tolower((unsigned char)name[length - 2]) == 'a' &&
           tolower((unsigned char)name[length - 1]) == 't';
}

static const char* resolveStage(const char* stage, int room)
{
    if (!stage[0])
        return nullptr;
    const char* anyRoom = nullptr;
    for (int i = 0; i < Warp::kDestinationCount; ++i) {
        const Warp::Destination& d = Warp::kDestinations[i];
        if (strncmp(d.stage, stage, 8) != 0)
            continue;
        if ((int)d.room == room)
            return d.name;
        if (!anyRoom)
            anyRoom = d.name;
    }
    return anyRoom;
}

static void setDestination(Entry* entry, const uint8_t* image, uint32_t size)
{
    if (size < kPeekSize)
        return;
    const dSv_player_return_place_c& place = ((const dSv_info_c*)image)->mReturnPlace;
    int i = 0;
    for (; i < 8 && place.mName[i]; ++i)
        entry->stage[i] = isprint((unsigned char)place.mName[i]) ? place.mName[i] : '?';
    entry->stage[i] = '\0';
    entry->place = resolveStage(entry->stage, place.mRoomNo);
}

static Entry* addEntry(int capacity)
{
    if (s_count >= capacity)
        return nullptr;
    if (s_count >= s_capacity) {
        const int grown = s_capacity ? s_capacity * 2 : 32;
        const int next = grown > capacity ? capacity : grown;
        Entry* entries = (Entry*)realloc(s_entries, sizeof(Entry) * (size_t)next);
        if (!entries)
            return nullptr;
        s_entries = entries;
        s_capacity = next;
    }
    Entry* entry = &s_entries[s_count++];
    memset(entry, 0, sizeof(*entry));
    return entry;
}

static unsigned char foldAscii(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int compareEntries(const void* left, const void* right)
{
    const Entry* a = (const Entry*)left;
    const Entry* b = (const Entry*)right;
    const int shorter = a->stemLength < b->stemLength ? a->stemLength : b->stemLength;
    for (int i = 0; i < shorter; ++i) {
        const unsigned char ca = foldAscii((unsigned char)a->path[i]);
        const unsigned char cb = foldAscii((unsigned char)b->path[i]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    if (a->stemLength != b->stemLength)
        return a->stemLength < b->stemLength ? -1 : 1;
    const int exact = strncmp(a->path, b->path, (size_t)shorter);
    return exact < 0 ? -1 : exact > 0 ? 1 : 0;
}

static void scanContent(const TabDef& tab)
{
    DIR* dir = opendir(tab.folder);
    if (!dir) {
        s_scanFailed = true;
        return;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (!endsWithDat(ent->d_name))
            continue;
        char full[256];
        const int n = snprintf(full, sizeof(full), "%s/%s", tab.folder, ent->d_name);
        struct stat st;
        if (n <= 0 || n >= (int)sizeof(full) || stat(full, &st) != 0 || S_ISDIR(st.st_mode))
            continue;
        const size_t stem = strlen(ent->d_name) - 4;
        if (stem >= (size_t)kPathMax)
            continue;
        Entry* entry = addEntry(kContentCapacity);
        if (!entry)
            break;
        memcpy(entry->path, ent->d_name, stem);
        entry->path[stem] = '\0';
        entry->stemLength = (int)stem;
        FILE* f = fopen(full, "rb");
        if (f) {
            const size_t read = fread(s_image, 1, kPeekSize, f);
            fclose(f);
            setDestination(entry, s_image, (uint32_t)read);
        }
    }
    closedir(dir);
}

static void visitPersonal(const Storage::DirEntry& item, void* context)
{
    const PersonalScan* scan = (const PersonalScan*)context;
    PersonalScan child;
    const int n = snprintf(child.dir, sizeof(child.dir), "%s/%s", scan->dir, item.name);
    if (n <= 0 || n >= (int)sizeof(child.dir))
        return;
    if (item.isDir) {
        if (scan->depth < kMaxScanDepth) {
            child.depth = scan->depth + 1;
            Storage::ListDir(child.dir, visitPersonal, &child);
        }
        return;
    }
    if (!endsWithDat(item.name))
        return;
    Entry* entry = addEntry(kPersonalCapacity);
    if (!entry)
        return;
    const char* relative = child.dir + strlen(kTabs[TAB_PERSONAL].folder) + 1;
    snprintf(entry->path, sizeof(entry->path), "%s", relative);
    entry->stemLength = (int)strlen(entry->path) - 4;
    uint32_t size = 0;
    if (Storage::ReadBinary(child.dir, s_image, sizeof(s_image), &size) == Storage::READ_OK)
        setDestination(entry, s_image, size);
}

static void scanPersonal(const TabDef& tab)
{
    PersonalScan root;
    snprintf(root.dir, sizeof(root.dir), "%s", tab.folder);
    root.depth = 0;
    if (!Storage::EnsureDir(root.dir) || !Storage::ListDir(root.dir, visitPersonal, &root))
        s_scanFailed = true;
}

static void scan(int tab)
{
    s_count = 0;
    s_scanFailed = false;
    s_listTab = tab;
    if (kTabs[tab].personal)
        scanPersonal(kTabs[tab]);
    else
        scanContent(kTabs[tab]);
    if (s_count > 1)
        qsort(s_entries, (size_t)s_count, sizeof(Entry), compareEntries);
    if (s_scanFailed)
        setStatus("Could not read the %s saves.", kTabs[tab].title);
    else
        s_status[0] = '\0';
}

static void requestLoad(int tab, const Entry& entry)
{
    if (s_request[0])
        return;
    if (kTabs[tab].personal) {
        snprintf(s_request, sizeof(s_request), "%s", entry.path);
        s_requestPersonal = true;
        setStatus("Reading %.63s ...", entry.path);
        return;
    }
    if (strlen(kTabs[tab].prefix) + (size_t)entry.stemLength > kEngineNameMax) {
        setStatus("Name too long for the game's loader: %.48s", entry.path);
        return;
    }
    snprintf(s_request, sizeof(s_request), "%s%.*s", kTabs[tab].prefix, entry.stemLength,
             entry.path);
    s_requestPersonal = false;
    setStatus("Loading %.63s ...", entry.path);
}

static void loadPersonal(const char* path)
{
    char relative[kPathMax + 16];
    snprintf(relative, sizeof(relative), "%s/%s", kTabs[TAB_PERSONAL].folder, path);
    uint32_t size = 0;
    const Storage::ReadResult r = Storage::ReadBinary(relative, s_image, sizeof(s_image), &size);
    if (r == Storage::READ_MISSING) {
        setStatus("Personal save was not found.");
        return;
    }
    if (r != Storage::READ_OK || (size != dSv_SERIALIZED_SIZE && size != kPersonalFileSize)) {
        Logger::LogWarn("[debugsave] %s rejected: read %d, %u bytes", path, (int)r,
                        (unsigned)size);
        setStatus("Could not read the personal save.");
        return;
    }
    Controller::EnsureSelected();
    if (SaveStates::BeginGameSaveLoad(path, s_image, dSv_SERIALIZED_SIZE))
        setStatus("Loading personal save ...");
    else
        setStatus("Save not loaded: the game is busy or the save is invalid.");
}

void Tick()
{
    if (!s_request[0])
        return;
    char name[kPathMax];
    snprintf(name, sizeof(name), "%s", s_request);
    s_request[0] = '\0';

    if (s_requestPersonal) {
        loadPersonal(name);
        return;
    }
    Controller::EnsureSelected();
    // A stage here would override the save's own resume rules, such as dungeon entrances.
    dDbgSave_request(name, nullptr, 0, 0, 0xFF, 0, 0, 0);
    Logger::Log("[debugsave] load requested: %s", name);
}

void OnApplicationStart()
{
    free(s_entries);
    s_entries = nullptr;
    s_count = 0;
    s_capacity = 0;
    s_listTab = -1;
    s_scanFailed = false;
    s_status[0] = '\0';
    s_request[0] = '\0';
    s_requestPersonal = false;
}

static void drawEntry(int tab, int index)
{
    const Entry& entry = s_entries[index];
    ImGui::PushID(index);
    ImGui::BeginDisabled(s_request[0] != '\0');
    if (ImGui::Button("Load"))
        requestLoad(tab, entry);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* where = entry.place ? entry.place : entry.stage;
    if (where[0])
        ImGui::Text("%.*s  (%s)", entry.stemLength, entry.path, where);
    else
        ImGui::Text("%.*s", entry.stemLength, entry.path);
    ImGui::PopID();
}

static void drawTab(int tab)
{
    if (!ImGui::BeginTabItem(kTabs[tab].title))
        return;
    if (s_listTab != tab)
        scan(tab);

    if (ImGui::Button("Refresh"))
        scan(tab);
    if (s_status[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s_status);
    }

    ImGui::BeginChild("##list", ImVec2(0.0f, 0.0f), true);
    if (!s_scanFailed && s_count == 0)
        ImGui::TextDisabled("%s", kTabs[tab].personal
                                      ? "Put .dat saves in tphd_tools/saves on the SD card."
                                      : "No saves here.");
    for (int i = 0; i < s_count; ++i)
        drawEntry(tab, i);
    ImGui::EndChild();
    ImGui::EndTabItem();
}

void DrawMenuItem()
{
    Ui::Window::Checkbox("Debug Save Loader", &s_open, kWindowName);
}

void DrawWindow()
{
    if (!s_open)
        return;
    ImGui::SetNextWindowPos(ImVec2(420.0f, 150.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (ImGui::BeginTabBar("##debug_save_tabs")) {
            for (int tab = 0; tab < TAB_COUNT; ++tab)
                drawTab(tab);
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
}
}
