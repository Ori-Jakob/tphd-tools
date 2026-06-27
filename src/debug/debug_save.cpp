// debug_save.cpp -- see debug_save.h.
//
// The game ships dev saves under /vol/content/DebugSaves/ in three folders:
//   DebugSaves/*.dat            -> "Debug" tab
//   DebugSaves/QASaves/*.dat    -> "QA" tab
//   DebugSaves/CSSaves/*.dat    -> "CS" tab
// Personal game saves live under tphd_tools/saves/*.dat on the active storage
// backend and appear in a fourth tab.
// A .dat is a raw dump of the info block (the loader just memcpy's it in), so we
// peek each file's player return-place stage (file offset 0x58) + room (0x61)
// and resolve a friendly name from the warp table to annotate the list. This is
// the exact record normal file-select loading passes to dComIfGp_setNextStage.
//
// Listing a folder means opening + reading every .dat (to peek its stage), so it
// runs on a BACKGROUND WORKER THREAD (own FSClient, OSMessageQueue mailboxes).
// Each finished operation is returned as an owned result message, so the render
// thread never races a shared scan buffer or blocks on directory/file reads. Shipped
// debug saves use the game's OWN debug-load path (dDbgSave_request ->
// FUN_02abceb0). Personal images are read through Storage, then handed to the
// save-state scene-transition bridge, which invokes the game's deserializer and
// resumes from the same return-place record as normal file-select loading.
#include "debug_save.h"

#include "imgui.h"
#include "overlay.h"            // ov::g_settings.controllerPref
#include "game/game.h"
#include "game/warp_table.h"
#include "tools/save_state.h"
#include "storage.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#include <coreinit/debug.h>
#include <coreinit/filesystem.h>
#include <coreinit/thread.h>
#include <coreinit/messagequeue.h>

namespace Debug {
namespace DebugSave {

enum { TAB_DEBUG = 0, TAB_QA, TAB_CS, TAB_PERSONAL };

// Personal .dat files may be either the raw 0xDF8 serialized save or the full
// 0xE00 game/debug file (serialized bytes plus an 8-byte trailer). CoreInit FS
// reads through 0x40-granular DMA buffers, so always allocate/read with the full
// aligned capacity. The load bridge consumes only GAME_DSV_SERIALIZED_SIZE.
static const uint32_t kPersonalSaveFileSize = 0x0E00u;
static_assert((kPersonalSaveFileSize & 0x3Fu) == 0,
              "Personal save file buffer must remain 0x40-aligned in size");

struct TabDef {
    const char* title;
    const char* folder;    // content dir to list
    const char* prefix;    // prepended to the stem for the loader's "%s"
    bool personal;         // source is SD tphd_tools/saves
};
static const TabDef kTabs[] = {
    { "Debug",    "/vol/content/DebugSaves",         "",         false },
    { "QA",       "/vol/content/DebugSaves/QASaves", "QASaves/", false },
    { "CS",       "/vol/content/DebugSaves/CSSaves", "CSSaves/", false },
    { "Personal", "tphd_tools/saves",                "",         true  },
};

static bool s_enabled = false;

void DrawMenuItem()     { ImGui::Checkbox("Debug Save Loader", &s_enabled); }
bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }

struct FileEntry {
    char stem[64];     // filename without ".dat"
    char label[128];   // "stem  (Friendly Stage)"
    char resumeStage[9]; // player return-place stage (actual load destination)
    int  resumeRoom;     // player return-place room
};

// ---- shared state -----------------------------------------------------------
// Display list (present thread only).
static const int kContentFileCapacity = 96;
static FileEntry* s_files = nullptr;
static int       s_fileCount  = 0;
static int       s_displayTab = -1;     // which tab s_files currently holds
static int       s_pendingTab = -1;     // one background scan at a time
static char      s_status[96] = "";

// A load requested from the UI (stem incl. subfolder prefix), executed in Tick().
static char s_pendingName[96] = "";
static bool s_personalBusy = false;
// ---- helpers (thread-agnostic; kWarpTable is const) -------------------------
static bool endsWithDat(const char* n)
{
    size_t l = strlen(n);
    return l > 4 && strcmp(n + l - 4, ".dat") == 0;
}

// Map a stage code (+ room) to a friendly map name via the warp table. Prefers
// an exact stage+room match, else any entry for that stage. Null if unknown.
static const char* resolveStage(const char* stage, int room)
{
    if (!stage[0])
        return nullptr;
    const char* anyStage = nullptr;
    for (size_t i = 0; i < sizeof(kWarpTable) / sizeof(kWarpTable[0]); ++i) {
        if (strncmp(kWarpTable[i].stage, stage, 8) != 0)
            continue;
        if ((int)kWarpTable[i].room == room)
            return kWarpTable[i].name;     // exact stage + room
        if (!anyStage)
            anyStage = kWarpTable[i].name;
    }
    return anyStage;
}

// ---- background worker (directory scan + per-file stage peek) ---------------
static OSThread       s_thread;
static bool           s_threadStarted = false;
static OSMessageQueue s_jobQueue;
static OSMessage      s_jobMsgBuf[8];
static OSMessageQueue s_resultQueue;
static OSMessage      s_resultMsgBuf[8];
static FSClient       s_contentClient;
static FSCmdBlock     s_contentCmd;
static __attribute__((aligned(16))) uint8_t s_stack[32 * 1024];

enum { JOB_SCAN = 0, JOB_LOAD_PERSONAL = 1 };
struct WorkerMessage {
    int op;
    char folder[160];
    char name[64];
    int tab;
    int count;
    int error;
    FileEntry* files;
    uint8_t* image;
};

static bool parseResumeDestination(const uint8_t* buf, uint32_t size,
                                   char* outStage, int* outRoom)
{
    const uint32_t needed = DSV_DAT_RETURN_PLACE_OFF + sizeof(dSv_player_return_place_c);
    if (!buf || size < needed)
        return false;
    memcpy(outStage, buf + DSV_DAT_RETURN_PLACE_OFF + DSV_RETURN_PLACE_OFF_NAME, 8);
    outStage[8] = '\0';
    *outRoom = (s8)buf[DSV_DAT_RETURN_PLACE_OFF + DSV_RETURN_PLACE_OFF_ROOM];
    return outStage[0] != '\0';
}

static void fillEntryLabel(FileEntry* fe, const char* stage, int room, bool peeked)
{
    memcpy(fe->resumeStage, stage, sizeof(fe->resumeStage));
    fe->resumeRoom = room;
    char stem[sizeof(fe->stem)];
    memcpy(stem, fe->stem, sizeof(stem));
    stem[sizeof(stem) - 1] = '\0';
    const char* friendly = peeked ? resolveStage(stage, room) : nullptr;
    if (friendly)
        snprintf(fe->label, sizeof(fe->label), "%s  (%s)", stem, friendly);
    else if (stage[0])
        snprintf(fe->label, sizeof(fe->label), "%s  (%s)", stem, stage);
    else
        snprintf(fe->label, sizeof(fe->label), "%s", stem);
}

// Peek the destination normal file-select loading uses without loading the .dat.
static bool peekResumeDestination(FSClient* c, FSCmdBlock* cmd, uint8_t* buf,
                                  const char* path, char* outStage, int* outRoom)
{
    FSFileHandle h;
    if (FSOpenFile(c, cmd, path, "r", &h, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return false;
    bool ok = false;
    const uint32_t needed = DSV_DAT_RETURN_PLACE_OFF + sizeof(dSv_player_return_place_c);
    FSStatus n = FSReadFile(c, cmd, buf, 1, needed, h, 0, FS_ERROR_FLAG_ALL);
    if (n >= 0)
        ok = parseResumeDestination(buf, (uint32_t)n, outStage, outRoom);
    FSCloseFile(c, cmd, h, FS_ERROR_FLAG_ALL);
    return ok;
}

static int worker(int argc, const char** argv)
{
    (void)argc; (void)argv;
    FSAddClient(&s_contentClient, FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(&s_contentCmd);
    uint8_t* peekBuf = (uint8_t*)memalign(0x40, 0x80);   // FS DMA buffer (0x40-aligned)

    for (;;) {
        OSMessage msg;
        OSReceiveMessage(&s_jobQueue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        WorkerMessage* job = (WorkerMessage*)msg.message;
        if (!job)
            continue;

        if (job->op == JOB_LOAD_PERSONAL) {
            uint32_t bytesRead = 0;
            TPHD_BREADCRUMB(
                "[tphd_tools][personal] worker read begin: name='%s' capacity=0x%X",
                job->name, (unsigned)kPersonalSaveFileSize);
            job->image = (uint8_t*)memalign(0x40, kPersonalSaveFileSize);
            if (job->image)
                memset(job->image, 0, kPersonalSaveFileSize);
            TPHD_BREADCRUMB(
                "[tphd_tools][personal] worker buffer: ptr=%p aligned40=%d",
                (void*)job->image,
                job->image && (((uintptr_t)job->image & 0x3Fu) == 0));
            Storage::ReadResult result = job->image
                ? Storage::LoadGameSave(job->name, job->image, kPersonalSaveFileSize,
                                        &bytesRead)
                : Storage::READ_ERROR;
            bool supportedSize = bytesRead == GAME_DSV_SERIALIZED_SIZE ||
                                 bytesRead == kPersonalSaveFileSize;
            TPHD_BREADCRUMB(
                "[tphd_tools][personal] worker read end: name='%s' result=%d bytes=0x%X "
                "supported=%d image=%p",
                job->name, (int)result, (unsigned)bytesRead, supportedSize,
                (void*)job->image);
            if (result != Storage::READ_OK || !supportedSize) {
                free(job->image);
                job->image = nullptr;
                job->error = result == Storage::READ_MISSING ? 1 : 2;
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal] worker rejecting image: error=%d",
                    job->error);
            }
        } else {
            const int capacity = job->tab == TAB_PERSONAL
                                     ? Storage::ListGameSaves(nullptr, 0)
                                     : kContentFileCapacity;
            job->files = capacity > 0
                             ? (FileEntry*)malloc(sizeof(FileEntry) * capacity)
                             : nullptr;
            int count = 0;
            if (capacity < 0 || (capacity > 0 && !job->files)) {
                job->error = 2;
            } else if (job->tab == TAB_PERSONAL) {
                Storage::StateEntry* entries =
                    capacity > 0
                        ? (Storage::StateEntry*)malloc(
                              sizeof(Storage::StateEntry) * capacity)
                        : nullptr;
                if (capacity == 0 || entries) {
                    int listed = Storage::ListGameSaves(entries, capacity);
                    if (listed > capacity)
                        listed = capacity;
                    for (int i = 0; i < listed; ++i) {
                        FileEntry* fe = &job->files[count++];
                        strncpy(fe->stem, entries[i].name, sizeof(fe->stem) - 1);
                        fe->stem[sizeof(fe->stem) - 1] = '\0';
                        char stage[9] = {};
                        int  room = 0;
                        uint32_t bytesRead = 0;
                        bool peeked =
                            peekBuf &&
                            Storage::LoadGameSave(fe->stem, peekBuf, 0x80,
                                                  &bytesRead) == Storage::READ_OK &&
                            parseResumeDestination(peekBuf, bytesRead, stage, &room);
                        fillEntryLabel(fe, stage, room, peeked);
                    }
                    free(entries);
                } else {
                    job->error = 2;
                }
            } else {
                FSDirectoryHandle dh;
                if (FSOpenDir(&s_contentClient, &s_contentCmd, job->folder, &dh,
                              FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
                    FSDirectoryEntry ent;
                    while (count < capacity &&
                           FSReadDir(&s_contentClient, &s_contentCmd, dh, &ent,
                                     FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
                        if ((ent.info.flags & FS_STAT_DIRECTORY) || !endsWithDat(ent.name))
                            continue;
                        FileEntry* fe = &job->files[count++];
                        size_t nl = strlen(ent.name) - 4;          // drop ".dat"
                        if (nl > sizeof(fe->stem) - 1)
                            nl = sizeof(fe->stem) - 1;
                        memcpy(fe->stem, ent.name, nl);
                        fe->stem[nl] = '\0';

                        char fullPath[512];
                        snprintf(fullPath, sizeof(fullPath), "%s/%s",
                                 job->folder, ent.name);
                        char stage[9] = {};
                        int  room = 0;
                        bool peeked = peekBuf &&
                                      peekResumeDestination(&s_contentClient, &s_contentCmd,
                                                            peekBuf,
                                                            fullPath, stage, &room);
                        fillEntryLabel(fe, stage, room, peeked);
                    }
                    FSCloseDir(&s_contentClient, &s_contentCmd, dh,
                               FS_ERROR_FLAG_ALL);
                } else {
                    job->error = 1;
                }
            }
            job->count = count;
        }

        OSMessage resultMsg;
        resultMsg.message = job;
        resultMsg.args[0] = resultMsg.args[1] = resultMsg.args[2] = 0;
        OSSendMessage(&s_resultQueue, &resultMsg, OS_MESSAGE_FLAGS_BLOCKING);
    }
    return 0;   // not reached
}

static void startWorker()
{
    if (s_threadStarted)
        return;
    OSInitMessageQueue(&s_jobQueue, s_jobMsgBuf,
                       (int32_t)(sizeof(s_jobMsgBuf) / sizeof(s_jobMsgBuf[0])));
    OSInitMessageQueue(&s_resultQueue, s_resultMsgBuf,
                       (int32_t)(sizeof(s_resultMsgBuf) / sizeof(s_resultMsgBuf[0])));
    void* stackTop = s_stack + sizeof(s_stack);
    if (!OSCreateThread(&s_thread, worker, 0, nullptr, stackTop, sizeof(s_stack), 16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        Logger::Log("[tphd_tools] debug-save scan thread create failed");
        return;
    }
    OSSetThreadName(&s_thread, "tphd_tools_dbgsave");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
}

// Ask the worker to (re)scan a tab's folder. Only one scan may be in flight:
// when the user switches rapidly, the active tab requests its scan after the
// current result is adopted instead of building a stale queue.
static void requestScan(int tab)
{
    if (tab < 0 || tab >= (int)(sizeof(kTabs) / sizeof(kTabs[0])))
        return;
    if (s_pendingTab != -1)
        return;
    startWorker();
    if (!s_threadStarted)
        return;
    WorkerMessage* job = (WorkerMessage*)calloc(1, sizeof(WorkerMessage));
    if (!job)
        return;
    job->op = JOB_SCAN;
    snprintf(job->folder, sizeof(job->folder), "%s", kTabs[tab].folder);
    job->name[0] = '\0';
    job->tab = tab;
    OSMessage msg;
    msg.message = job;
    msg.args[0] = msg.args[1] = msg.args[2] = 0;
    if (!OSSendMessage(&s_jobQueue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        free(job);
        return;
    }
    s_pendingTab = tab;
}

static void requestPersonalLoad(const char* name)
{
    TPHD_BREADCRUMB(
        "[tphd_tools][personal] UI request: name='%s' busy=%d pendingName=%d "
        "pendingTab=%d",
        name ? name : "(null)", s_personalBusy, s_pendingName[0] != '\0',
        s_pendingTab);
    if (!name || !name[0] || s_personalBusy || s_pendingName[0] ||
        s_pendingTab != -1) {
        TPHD_BREADCRUMB("[tphd_tools][personal] UI request rejected by guard");
        return;
    }
    startWorker();
    if (!s_threadStarted) {
        TPHD_BREADCRUMB("[tphd_tools][personal] UI request rejected: worker unavailable");
        return;
    }
    WorkerMessage* job = (WorkerMessage*)calloc(1, sizeof(WorkerMessage));
    if (!job) {
        TPHD_BREADCRUMB("[tphd_tools][personal] UI request rejected: job allocation failed");
        return;
    }
    job->op = JOB_LOAD_PERSONAL;
    job->folder[0] = '\0';
    strncpy(job->name, name, sizeof(job->name) - 1);
    job->name[sizeof(job->name) - 1] = '\0';
    job->tab = TAB_PERSONAL;

    s_personalBusy = true;
    OSMessage msg;
    msg.message = job;
    msg.args[0] = msg.args[1] = msg.args[2] = 0;
    if (!OSSendMessage(&s_jobQueue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        s_personalBusy = false;
        TPHD_BREADCRUMB("[tphd_tools][personal] UI request rejected: job queue full");
        free(job);
        return;
    }
    TPHD_BREADCRUMB(
        "[tphd_tools][personal] UI request queued: name='%s' job=%p",
        job->name, (void*)job);
    snprintf(s_status, sizeof(s_status), "Reading %.63s ...", name);
}

// ---- per-frame tick (present thread) ----------------------------------------
void Tick()
{
    bool beganPersonalLoad = false;

    // Adopt complete, worker-owned results. The message queue provides the
    // cross-thread handoff; no list or image is touched by both threads.
    OSMessage resultMsg;
    while (s_threadStarted &&
           OSReceiveMessage(&s_resultQueue, &resultMsg, OS_MESSAGE_FLAGS_NONE)) {
        WorkerMessage* result = (WorkerMessage*)resultMsg.message;
        if (!result)
            continue;

        if (result->op == JOB_SCAN) {
            s_pendingTab = -1;

            int n = result->count;
            if (n < 0)
                n = 0;
            bool validTab = result->tab >= 0 &&
                            result->tab < (int)(sizeof(kTabs) / sizeof(kTabs[0]));
            if (validTab && !result->error && (n == 0 || result->files)) {
                free(s_files);
                s_files = result->files;
                result->files = nullptr;
                s_fileCount  = n;
                s_displayTab = result->tab;
                s_status[0] = '\0';
            } else {
                free(s_files);
                s_files = nullptr;
                s_fileCount = 0;
                s_displayTab = validTab ? result->tab : -1;
                snprintf(s_status, sizeof(s_status), "Could not scan %s.",
                         validTab ? kTabs[result->tab].title : "save tab");
            }
        } else {
            s_personalBusy = false;
            TPHD_BREADCRUMB(
                "[tphd_tools][personal] result adopted: name='%s' result=%p image=%p "
                "error=%d padMgr=%p padMode=%u link=%p",
                result->name, (void*)result, (void*)result->image, result->error,
                dPad_getControlPadMgr(), (unsigned)dPad_getControllerMode(),
                (void*)dComIfGp_getPlayer());
            if (result->error || !result->image) {
                snprintf(s_status, sizeof(s_status),
                         result->error == 1 ? "Personal save was not found."
                                            : "Could not read the personal save.");
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal] result rejected before load bridge");
            } else {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal] ensuring controller: pref=%d modeBefore=%u",
                    ov::g_settings.controllerPref, (unsigned)dPad_getControllerMode());
                dPad_ensureControllerSelected(ov::g_settings.controllerPref);
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal] controller ready: mgr=%p modeAfter=%u selected=%d",
                    dPad_getControlPadMgr(), (unsigned)dPad_getControllerMode(),
                    dPad_isControllerSelected());
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal] calling BeginGameSaveLoad: image=%p size=0x%X",
                    (void*)result->image, (unsigned)GAME_DSV_SERIALIZED_SIZE);
                if (Tools::SaveState::BeginGameSaveLoad(
                        result->name, result->image, GAME_DSV_SERIALIZED_SIZE)) {
                    snprintf(s_status, sizeof(s_status), "Loading personal save ...");
                    TPHD_BREADCRUMB(
                        "[tphd_tools][personal] BeginGameSaveLoad accepted");
                    beganPersonalLoad = true;
                } else {
                    snprintf(s_status, sizeof(s_status),
                             "Game is busy; personal save was not loaded.");
                    TPHD_BREADCRUMB(
                        "[tphd_tools][personal] BeginGameSaveLoad rejected");
                }
            }
        }

        if (result->op == JOB_LOAD_PERSONAL)
            TPHD_BREADCRUMB(
                "[tphd_tools][personal] freeing worker result: files=%p image=%p result=%p",
                (void*)result->files, (void*)result->image, (void*)result);
        free(result->files);
        free(result->image);
        free(result);
    }
    if (beganPersonalLoad)
        return;

    // Run a pending load (off the ImGui draw).
    if (!s_pendingName[0])
        return;
    char name[96];
    strncpy(name, s_pendingName, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    s_pendingName[0] = '\0';

    // Loading from the title screen (before the boot controller-select) crashed:
    // the debug-load runs gameplay against a controller provider that was never
    // finalized. Make the same selection the select screen makes (default GamePad)
    // if the player hasn't chosen yet, so the load is safe from there. No-op once a
    // controller is already chosen (in-game), so it never overrides a Pro player.
    dPad_ensureControllerSelected(ov::g_settings.controllerPref);

    // Do not pass field-last-stay as an explicit stage. FUN_02abc630 treats a
    // non-empty request stage as a hard dComIfGp_setNextStage override, which
    // bypasses normal save-resume rules such as loading a dungeon save at its
    // entrance. A null stage becomes the engine's empty-string default, leaving
    // destination selection to the loaded save's restart data.
    dDbgSave_request(name, nullptr, 0, 0, 0xFF, 0, 0, 0);
    snprintf(s_status, sizeof(s_status), "Loading %.80s ...", name);
    Logger::Log("[tphd_tools] debug save load requested: %s (normal resume)", name);
}

// ---- window -----------------------------------------------------------------
static void drawTab(int tab)
{
    if (!ImGui::BeginTabItem(kTabs[tab].title))
        return;
    // Kick off a background scan when this tab's list isn't loaded yet.
    if (s_displayTab != tab && s_pendingTab != tab)
        requestScan(tab);

    if (ImGui::Button("Refresh")) {
        s_displayTab = -1;
        requestScan(tab);
    }
    if (s_status[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s_status);
    }

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    if (s_displayTab != tab) {
        if (s_pendingTab == tab) {
            ImGui::TextDisabled("Scanning %s ...", kTabs[tab].folder);
        } else if (s_pendingTab >= 0 &&
                   s_pendingTab < (int)(sizeof(kTabs) / sizeof(kTabs[0]))) {
            ImGui::TextDisabled("Waiting for %s scan ...", kTabs[s_pendingTab].title);
        } else {
            ImGui::TextDisabled("Preparing scan ...");
        }
    } else if (s_fileCount == 0) {
        ImGui::TextDisabled("No .dat files here.");
    } else {
        for (int i = 0; i < s_fileCount; ++i) {
            ImGui::PushID(i);
            if (ImGui::Button("Load")) {
                if (kTabs[tab].personal) {
                    requestPersonalLoad(s_files[i].stem);
                } else if (!s_personalBusy) {
                    snprintf(s_pendingName, sizeof(s_pendingName), "%.31s%.63s",
                             kTabs[tab].prefix, s_files[i].stem);
                    snprintf(s_status, sizeof(s_status), "Loading %.63s ...",
                             s_files[i].stem);
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(s_files[i].label);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(420.0f, 150.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug Save Loader", &s_enabled, flags)) {
        ImGui::TextDisabled("Read-only game saves. Uses normal save-resume rules.");
        if (ImGui::BeginTabBar("##dbgtabs")) {
            drawTab(TAB_DEBUG);
            drawTab(TAB_QA);
            drawTab(TAB_CS);
            drawTab(TAB_PERSONAL);
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace DebugSave
} // namespace Debug
