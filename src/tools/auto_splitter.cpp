// auto_splitter.cpp -- native TPHD timer/autosplitter.
//
// The parser intentionally accepts dragonbane0's original Wii U split schema.
// Its addresses are rebased by comparing the JSON's CurrentStage alias with this
// build's mStartStage address, allowing the supplied NTSC file to work unchanged.
// SD I/O + JSON parsing run on a worker; the present-thread hot path only reads a
// handful of game values and OSGetTime().
#include "tools/auto_splitter.h"

#include "storage.h"
#include "logger.h"
#include "overlay.h"
#include "game/game.h"

#include "imgui.h"
#include "cJSON.h"

#include <coreinit/memheap.h>
#include <coreinit/memorymap.h>
#include <coreinit/messagequeue.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace AutoSplitter {

static const int kMaxFiles = 32;
static const int kMaxSplits = 64;
static const int kMaxConditions = 12;
static const int kMaxOffsets = 8;
static const int kMaxString = 40;
static const uint32_t kMaxJsonBytes = 512 * 1024;

struct Condition {
    bool enabled;
    bool usePointer;
    uint8_t addressType;
    uint8_t comparisonType;
    uint8_t offsetCount;
    uint32_t address;
    uint32_t offsets[kMaxOffsets];
    uint32_t integerValue;
    float floatValue;
    char stringValue[kMaxString];
};

struct ConditionSet {
    bool disabled;
    uint8_t count;
    Condition conditions[kMaxConditions];
};

struct Split {
    char name[96];
    ConditionSet trigger;
};

struct TphdAliases {
    uint32_t nextSpawn;
    uint32_t isLoading;
};

struct Definition {
    uint64_t titleId;
    int64_t relocation;
    char displayName[96];
    char sourcePath[160];
    char historyKey[96];
    ConditionSet start;
    ConditionSet end;
    ConditionSet loading;
    int splitCount;             // includes synthetic final "Finish"
    Split splits[kMaxSplits];
    TphdAliases tp;
};

struct History {
    int splitCount;
    uint64_t personalBestUs;
    uint64_t pbSplitsUs[kMaxSplits];
    // Legacy/fallback copy. New writes also mirror golds into their own JSON.
    uint64_t bestSegmentsUs[kMaxSplits];
};

struct Golds {
    int splitCount;
    uint64_t segmentUs[kMaxSplits];
};

struct FileList {
    int count;
    Storage::SplitFileEntry entries[kMaxFiles];
};

enum JobOp { JOB_SCAN = 1, JOB_LOAD, JOB_SAVE_HISTORY, JOB_SAVE_GOLDS };
struct Job {
    JobOp op;
    char path[160];
    char name[96];
    char key[96];
    History history;
    Golds golds;
};

struct Result {
    JobOp op;
    int error;
    FileList* files;
    Definition* definition;
    History history;
    Golds golds;
};

enum RunState { RUN_IDLE = 0, RUN_RUNNING, RUN_FINISHED };

static bool s_initialized = false;
static bool s_enabled = false;
static bool s_autoStart = true;
static bool s_removeLoads = true;
static float s_deltaPreviewSeconds = 10.0f;
static bool s_initialsWhenDeltaShown = false;
static char s_selectedPath[160] = {};

static OSThread s_thread;
static bool s_threadStarted = false;
static OSMessageQueue s_jobQueue;
static OSMessageQueue s_resultQueue;
static OSMessage s_jobMessages[6];
static OSMessage s_resultMessages[6];
static __attribute__((aligned(16))) uint8_t s_stack[24 * 1024];

static FileList* s_files = nullptr;
static Definition* s_definition = nullptr;
static History s_history = {};
static Golds s_golds = {};
static bool s_scanBusy = false;
static bool s_loadBusy = false;
static char s_status[160] = "Place a split JSON on the SD card";

static RunState s_runState = RUN_IDLE;
static int s_currentSplit = 0;
static uint64_t s_runSplitsUs[kMaxSplits] = {};
static OSTime s_startTick = 0;
static OSTime s_finishTick = 0;
static OSTime s_loadStartTick = 0;
static OSTime s_pausedTicks = 0;
static bool s_timerLoading = false;

static bool s_prevStartCondition = false;
static bool s_prevEndCondition = false;
static bool s_prevRawLoading = false;
static bool s_haveLoadingSample = false;
static bool s_ignoreCurrentLoad = false;
static bool s_lastTitleSceneLoad = false;
static int s_tpStartState = 0;

#ifdef TPHD_TOOLS_DEBUG
struct StartDebugSnapshot {
    bool enabled;
    bool autoStart;
    bool hasDefinition;
    bool startDisabled;
    uint8_t runState;
    uint8_t detectorState;
    uint8_t rawLoading;
    uint8_t timestampSet;
    uint32_t nameScene;
    uint32_t rawFileSelect;
    uint32_t validFileSelect;
    uint8_t selectionPath;
    uint8_t fileState;
    uint8_t loadResult;
    uint8_t selectedSlot;
    uint8_t heroChoice;
};

static StartDebugSnapshot s_lastStartDebug = {};
static bool s_haveStartDebug = false;
static uint32_t s_splitDebugFrames = 0;
static uint32_t s_lastSplitTraceHash = 0;
static int s_lastTracedSplit = -1;
#endif

static uint32_t fnv1a(const char* text)
{
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)text; p && *p; ++p) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

static void copyText(char* out, int outSize, const char* text)
{
    if (out && outSize > 0)
        snprintf(out, outSize, "%s", text ? text : "");
}

static void makeHistoryKey(const char* name, const char* path, char* out, int outSize)
{
    char safe[56];
    int n = 0;
    for (const char* p = name; p && *p && n < (int)sizeof(safe) - 1; ++p) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            safe[n++] = c;
        else if (n > 0 && safe[n - 1] != '_')
            safe[n++] = '_';
    }
    safe[n] = '\0';
    snprintf(out, outSize, "%s_%08x", safe[0] ? safe : "splits",
             (unsigned)fnv1a(path));
}

static bool validRange(uint32_t address, uint32_t size)
{
    if (!address || !size || address > 0xF5FFFFFFu || address + size < address)
        return false;
    uint32_t end = address + size - 1;

    // Zelda.rpx globals live in the module's fixed data window. Cemu does not
    // necessarily report this guest range through OSIsAddressValid(), and it
    // is not owned by a MEM heap. Split definitions use these addresses as
    // direct values and as the roots of pointer chains.
    if (address >= 0x10000000u && end < 0x11000000u)
        return true;

    if (OSIsAddressValid(address) && OSIsAddressValid(end))
        return true;

    // Cemu's expanded Zelda heaps can sit above the range recognized by
    // OSIsAddressValid(). Ask coreinit's heap registry instead of treating the
    // entire user-space window as readable: scene heaps are removed from this
    // registry during teardown, which prevents stale pointers from being
    // dereferenced across a load.
    MEMHeapHeader* heap = MEMFindContainHeap((void*)(uintptr_t)address);
    if (!heap)
        return false;
    uint32_t heapStart = (uint32_t)(uintptr_t)heap->dataStart;
    uint32_t heapEnd = (uint32_t)(uintptr_t)heap->dataEnd;
    return address >= heapStart && end < heapEnd;
}

static uint32_t translateAddress(uint32_t address, int64_t relocation)
{
    if (address == 0xFFFFFFFFu || address < 0x10000000u || address >= 0xF6000000u)
        return address;
    int64_t translated = (int64_t)address - relocation;
    if (translated <= 0 || translated > 0xFFFFFFFFll)
        return 0;
    return (uint32_t)translated;
}

static bool parseCondition(const cJSON* item, int64_t relocation, Condition* out)
{
    if (!item || !out || !cJSON_IsObject(item))
        return false;
    cJSON* use = cJSON_GetObjectItemCaseSensitive(item, "UsePointer");
    cJSON* type = cJSON_GetObjectItemCaseSensitive(item, "AddressType");
    cJSON* base = cJSON_GetObjectItemCaseSensitive(item, "BaseAddress");
    cJSON* comp = cJSON_GetObjectItemCaseSensitive(item, "ComparisonType");
    cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "Value");
    if (!cJSON_IsBool(use) || !cJSON_IsNumber(type) || !cJSON_IsNumber(base) ||
        !cJSON_IsNumber(comp) || !value)
        return false;

    memset(out, 0, sizeof(*out));
    out->enabled = true;
    out->usePointer = cJSON_IsTrue(use);
    out->addressType = (uint8_t)type->valueint;
    out->comparisonType = (uint8_t)comp->valueint;
    uint32_t rawAddress = (uint32_t)base->valuedouble;
    out->address = translateAddress(rawAddress, relocation);
    if (out->addressType > 4 || out->comparisonType > 7)
        return false;
    if (out->addressType == 4 && out->comparisonType > 1)
        return false;
    if ((out->comparisonType == 6 || out->comparisonType == 7) &&
        out->addressType != 0)
        return false;

    if (out->usePointer) {
        cJSON* offsets = cJSON_GetObjectItemCaseSensitive(item, "Offsets");
        if (!cJSON_IsArray(offsets))
            return false;
        int count = cJSON_GetArraySize(offsets);
        if (count <= 0 || count > kMaxOffsets)
            return false;
        out->offsetCount = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            cJSON* offset = cJSON_GetArrayItem(offsets, i);
            if (!cJSON_IsNumber(offset))
                return false;
            out->offsets[i] = (uint32_t)offset->valuedouble;
        }
    }

    if (out->addressType == 4) {
        if (!cJSON_IsString(value) || !value->valuestring ||
            strlen(value->valuestring) >= sizeof(out->stringValue))
            return false;
        strncpy(out->stringValue, value->valuestring, sizeof(out->stringValue) - 1);
    } else if (out->addressType == 3) {
        if (!cJSON_IsNumber(value))
            return false;
        out->floatValue = (float)value->valuedouble;
    } else {
        if (!cJSON_IsNumber(value))
            return false;
        out->integerValue = (uint32_t)value->valuedouble;
        if ((out->comparisonType == 6 || out->comparisonType == 7) &&
            out->integerValue > 7)
            return false;
    }

    if (!out->usePointer && out->addressType == 0 &&
        rawAddress == 0xFFFFFFFFu && out->comparisonType == 0 &&
        out->integerValue == 0xFFu)
        out->enabled = false;
    return true;
}

static bool parseConditionSet(const cJSON* splitItem, int64_t relocation,
                              ConditionSet* out)
{
    if (!splitItem || !out)
        return false;
    cJSON* array = cJSON_GetObjectItemCaseSensitive(splitItem, "Conditions");
    if (!cJSON_IsArray(array))
        return false;
    int count = cJSON_GetArraySize(array);
    if (count <= 0 || count > kMaxConditions)
        return false;
    memset(out, 0, sizeof(*out));
    out->count = (uint8_t)count;
    for (int i = 0; i < count; ++i) {
        if (!parseCondition(cJSON_GetArrayItem(array, i), relocation,
                            &out->conditions[i]))
            return false;
        if (!out->conditions[i].enabled)
            out->disabled = true;
    }
    return true;
}

static void parseAliases(const cJSON* root, Definition* def, uint32_t* currentStageRaw)
{
    cJSON* aliases = cJSON_GetObjectItemCaseSensitive(root, "Alias");
    if (!cJSON_IsArray(aliases))
        return;
    int count = cJSON_GetArraySize(aliases);
    for (int i = 0; i < count; ++i) {
        cJSON* alias = cJSON_GetArrayItem(aliases, i);
        cJSON* name = cJSON_GetObjectItemCaseSensitive(alias, "Name");
        cJSON* address = cJSON_GetObjectItemCaseSensitive(alias, "Address");
        if (!cJSON_IsString(name) || !name->valuestring || !cJSON_IsNumber(address))
            continue;
        uint32_t raw = (uint32_t)address->valuedouble;
        if (strcmp(name->valuestring, "CurrentStage") == 0)
            *currentStageRaw = raw;
    }
    def->relocation = *currentStageRaw
        ? (int64_t)*currentStageRaw - (int64_t)GAME_ADDR_startStage
        : 0;
    for (int i = 0; i < count; ++i) {
        cJSON* alias = cJSON_GetArrayItem(aliases, i);
        cJSON* name = cJSON_GetObjectItemCaseSensitive(alias, "Name");
        cJSON* address = cJSON_GetObjectItemCaseSensitive(alias, "Address");
        if (!cJSON_IsString(name) || !name->valuestring || !cJSON_IsNumber(address))
            continue;
        uint32_t mapped =
            translateAddress((uint32_t)address->valuedouble, def->relocation);
        if (strcmp(name->valuestring, "NextSpawn") == 0) def->tp.nextSpawn = mapped;
        else if (strcmp(name->valuestring, "IsLoading") == 0) def->tp.isLoading = mapped;
    }
}

static Definition* parseDefinition(const char* text, const char* name, const char* path,
                                   char* error, int errorSize)
{
    cJSON* root = cJSON_Parse(text);
    if (!root) {
        snprintf(error, errorSize, "JSON parse failed");
        return nullptr;
    }
    Definition* def = (Definition*)calloc(1, sizeof(Definition));
    if (!def) {
        cJSON_Delete(root);
        snprintf(error, errorSize, "Out of memory");
        return nullptr;
    }
    copyText(def->displayName, sizeof(def->displayName), name);
    copyText(def->sourcePath, sizeof(def->sourcePath), path);
    makeHistoryKey(name, path, def->historyKey, sizeof(def->historyKey));
    cJSON* title = cJSON_GetObjectItemCaseSensitive(root, "TitleID");
    if (cJSON_IsNumber(title))
        def->titleId = (uint64_t)title->valuedouble;

    uint32_t currentStageRaw = 0;
    parseAliases(root, def, &currentStageRaw);
    cJSON* splits = cJSON_GetObjectItemCaseSensitive(root, "Splits");
    if (!cJSON_IsArray(splits) || cJSON_GetArraySize(splits) < 3) {
        snprintf(error, errorSize, "Splits array is missing or too small");
        free(def);
        cJSON_Delete(root);
        return nullptr;
    }

    int genericCount = 0;
    int splitItems = cJSON_GetArraySize(splits);
    for (int i = 0; i < splitItems; ++i) {
        cJSON* split = cJSON_GetArrayItem(splits, i);
        cJSON* splitName = cJSON_GetObjectItemCaseSensitive(split, "Name");
        if (!cJSON_IsString(splitName) || !splitName->valuestring) {
            snprintf(error, errorSize, "Split %d has no name", i);
            free(def);
            cJSON_Delete(root);
            return nullptr;
        }
        ConditionSet* target = nullptr;
        if (strcmp(splitName->valuestring, "_Start") == 0)
            target = &def->start;
        else if (strcmp(splitName->valuestring, "_End") == 0)
            target = &def->end;
        else if (strcmp(splitName->valuestring, "_Loading") == 0)
            target = &def->loading;
        else {
            if (genericCount >= kMaxSplits - 1) {
                snprintf(error, errorSize, "Too many splits (max %d)", kMaxSplits - 1);
                free(def);
                cJSON_Delete(root);
                return nullptr;
            }
            Split* dst = &def->splits[genericCount++];
            copyText(dst->name, sizeof(dst->name), splitName->valuestring);
            target = &dst->trigger;
        }
        if (!parseConditionSet(split, def->relocation, target)) {
            snprintf(error, errorSize, "Invalid conditions for '%s'",
                     splitName->valuestring);
            free(def);
            cJSON_Delete(root);
            return nullptr;
        }
    }
    if (def->end.count == 0) {
        snprintf(error, errorSize, "_End is missing");
        free(def);
        cJSON_Delete(root);
        return nullptr;
    }
    strncpy(def->splits[genericCount].name, "Finish",
            sizeof(def->splits[genericCount].name) - 1);
    def->splitCount = genericCount + 1;
    cJSON_Delete(root);
    return def;
}

static void parseHistory(const char* text, int splitCount, History* out)
{
    memset(out, 0, sizeof(*out));
    out->splitCount = splitCount;
    if (!text)
        return;
    cJSON* root = cJSON_Parse(text);
    if (!root)
        return;
    cJSON* count = cJSON_GetObjectItemCaseSensitive(root, "splitCount");
    if (!cJSON_IsNumber(count) || count->valueint != splitCount) {
        cJSON_Delete(root);
        return;
    }
    cJSON* pb = cJSON_GetObjectItemCaseSensitive(root, "personalBestUs");
    if (cJSON_IsNumber(pb))
        out->personalBestUs = (uint64_t)pb->valuedouble;
    cJSON* pbSplits = cJSON_GetObjectItemCaseSensitive(root, "pbSplitsUs");
    cJSON* golds = cJSON_GetObjectItemCaseSensitive(root, "bestSegmentsUs");
    if (cJSON_IsArray(pbSplits) && cJSON_GetArraySize(pbSplits) == splitCount)
        for (int i = 0; i < splitCount; ++i) {
            cJSON* v = cJSON_GetArrayItem(pbSplits, i);
            if (cJSON_IsNumber(v)) out->pbSplitsUs[i] = (uint64_t)v->valuedouble;
        }
    if (cJSON_IsArray(golds) && cJSON_GetArraySize(golds) == splitCount)
        for (int i = 0; i < splitCount; ++i) {
            cJSON* v = cJSON_GetArrayItem(golds, i);
            if (cJSON_IsNumber(v)) out->bestSegmentsUs[i] = (uint64_t)v->valuedouble;
    }
    cJSON_Delete(root);
}

static void parseGolds(const char* text, int splitCount,
                       const uint64_t* fallbackSegments, Golds* out)
{
    memset(out, 0, sizeof(*out));
    out->splitCount = splitCount;
    if (fallbackSegments)
        memcpy(out->segmentUs, fallbackSegments, sizeof(uint64_t) * splitCount);
    if (!text)
        return;

    cJSON* root = cJSON_Parse(text);
    if (!root)
        return;
    cJSON* count = cJSON_GetObjectItemCaseSensitive(root, "splitCount");
    if (!cJSON_IsNumber(count) || count->valueint != splitCount) {
        cJSON_Delete(root);
        return;
    }
    cJSON* golds = cJSON_GetObjectItemCaseSensitive(root, "segmentGoldUs");
    if (!golds)
        golds = cJSON_GetObjectItemCaseSensitive(root, "bestSegmentsUs");
    if (cJSON_IsArray(golds) && cJSON_GetArraySize(golds) == splitCount) {
        memset(out->segmentUs, 0, sizeof(out->segmentUs));
        for (int i = 0; i < splitCount; ++i) {
            cJSON* v = cJSON_GetArrayItem(golds, i);
            if (cJSON_IsNumber(v))
                out->segmentUs[i] = (uint64_t)v->valuedouble;
        }
    }
    cJSON_Delete(root);
}

static char* serializeHistory(const History& history)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return nullptr;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "splitCount", history.splitCount);
    cJSON_AddNumberToObject(root, "personalBestUs", (double)history.personalBestUs);
    cJSON* pb = cJSON_AddArrayToObject(root, "pbSplitsUs");
    cJSON* golds = cJSON_AddArrayToObject(root, "bestSegmentsUs");
    if (!pb || !golds) {
        cJSON_Delete(root);
        return nullptr;
    }
    for (int i = 0; i < history.splitCount; ++i) {
        cJSON_AddItemToArray(pb, cJSON_CreateNumber((double)history.pbSplitsUs[i]));
        cJSON_AddItemToArray(golds,
                             cJSON_CreateNumber((double)history.bestSegmentsUs[i]));
    }
    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

static char* serializeGolds(const Golds& golds)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return nullptr;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "splitCount", golds.splitCount);
    cJSON* segments = cJSON_AddArrayToObject(root, "segmentGoldUs");
    if (!segments) {
        cJSON_Delete(root);
        return nullptr;
    }
    for (int i = 0; i < golds.splitCount; ++i)
        cJSON_AddItemToArray(segments,
                             cJSON_CreateNumber((double)golds.segmentUs[i]));
    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

static void sendResult(Result* result)
{
    OSMessage msg = {};
    msg.message = result;
    if (!OSSendMessage(&s_resultQueue, &msg, OS_MESSAGE_FLAGS_BLOCKING)) {
        if (result) {
            free(result->files);
            free(result->definition);
            free(result);
        }
    }
}

static int worker(int argc, const char** argv)
{
    (void)argc;
    (void)argv;
    for (;;) {
        OSMessage message;
        OSReceiveMessage(&s_jobQueue, &message, OS_MESSAGE_FLAGS_BLOCKING);
        Job* job = (Job*)message.message;
        if (!job)
            continue;
        Result* result = (Result*)calloc(1, sizeof(Result));
        if (!result) {
            free(job);
            continue;
        }
        result->op = job->op;
        if (job->op == JOB_SCAN) {
            result->files = (FileList*)calloc(1, sizeof(FileList));
            if (!result->files)
                result->error = 1;
            else
                result->files->count =
                    Storage::ListSplitFiles(result->files->entries, kMaxFiles);
        } else if (job->op == JOB_LOAD) {
            char* text = nullptr;
            Storage::ReadResult r =
                Storage::LoadSplitFile(job->path, &text, kMaxJsonBytes);
            if (r == Storage::READ_OK) {
                char parseError[128] = {};
                result->definition =
                    parseDefinition(text, job->name, job->path,
                                    parseError, sizeof(parseError));
                free(text);
                if (!result->definition) {
                    result->error = 2;
                    Logger::LogError("[tphd_tools] autosplitter: %s", parseError);
                } else {
                    char* historyText = nullptr;
                    Storage::ReadResult hr =
                        Storage::LoadSplitHistory(result->definition->historyKey,
                                                  &historyText, 64 * 1024);
                    parseHistory(hr == Storage::READ_OK ? historyText : nullptr,
                                 result->definition->splitCount, &result->history);
                    free(historyText);

                    char* goldText = nullptr;
                    Storage::ReadResult gr =
                        Storage::LoadSplitGolds(result->definition->historyKey,
                                                &goldText, 64 * 1024);
                    parseGolds(gr == Storage::READ_OK ? goldText : nullptr,
                               result->definition->splitCount,
                               result->history.bestSegmentsUs, &result->golds);
                    free(goldText);
                }
            } else {
                result->error = r == Storage::READ_MISSING ? 1 : 3;
            }
        } else if (job->op == JOB_SAVE_HISTORY) {
            char* text = serializeHistory(job->history);
            if (!text || !Storage::SaveSplitHistory(job->key, text))
                result->error = 1;
            cJSON_free(text);
        } else if (job->op == JOB_SAVE_GOLDS) {
            char* text = serializeGolds(job->golds);
            if (!text || !Storage::SaveSplitGolds(job->key, text))
                result->error = 1;
            cJSON_free(text);
        }
        free(job);
        sendResult(result);
    }
    return 0;
}

static void startWorker()
{
    if (s_threadStarted)
        return;
    OSInitMessageQueue(&s_jobQueue, s_jobMessages,
                       sizeof(s_jobMessages) / sizeof(s_jobMessages[0]));
    OSInitMessageQueue(&s_resultQueue, s_resultMessages,
                       sizeof(s_resultMessages) / sizeof(s_resultMessages[0]));
    if (!OSCreateThread(&s_thread, worker, 0, nullptr, s_stack + sizeof(s_stack),
                        sizeof(s_stack), 16, OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        snprintf(s_status, sizeof(s_status), "Could not create autosplitter worker");
        return;
    }
    OSSetThreadName(&s_thread, "tphd_tools_splits");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
}

static bool postJob(Job* job)
{
    startWorker();
    if (!s_threadStarted) {
        free(job);
        return false;
    }
    OSMessage msg = {};
    msg.message = job;
    if (!OSSendMessage(&s_jobQueue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        free(job);
        return false;
    }
    return true;
}

static void requestScan()
{
    if (s_scanBusy)
        return;
    Job* job = (Job*)calloc(1, sizeof(Job));
    if (!job)
        return;
    job->op = JOB_SCAN;
    if (postJob(job)) {
        s_scanBusy = true;
        snprintf(s_status, sizeof(s_status), "Scanning split files...");
    }
}

static const Storage::SplitFileEntry* findFile(const char* path)
{
    if (!s_files || !path)
        return nullptr;
    for (int i = 0; i < s_files->count; ++i)
        if (strcmp(s_files->entries[i].path, path) == 0)
            return &s_files->entries[i];
    return nullptr;
}

static void requestLoad(const Storage::SplitFileEntry* file)
{
    if (!file || s_loadBusy)
        return;
    Job* job = (Job*)calloc(1, sizeof(Job));
    if (!job)
        return;
    job->op = JOB_LOAD;
    copyText(job->path, sizeof(job->path), file->path);
    copyText(job->name, sizeof(job->name), file->name);
    if (postJob(job)) {
        s_loadBusy = true;
        snprintf(s_status, sizeof(s_status), "Loading %s...", file->name);
    }
}

static void queueHistorySave()
{
    if (!s_definition)
        return;
    Job* job = (Job*)calloc(1, sizeof(Job));
    if (!job)
        return;
    job->op = JOB_SAVE_HISTORY;
    copyText(job->key, sizeof(job->key), s_definition->historyKey);
    job->history = s_history;
    postJob(job);
}

static void queueGoldsSave()
{
    if (!s_definition)
        return;
    Job* job = (Job*)calloc(1, sizeof(Job));
    if (!job)
        return;
    job->op = JOB_SAVE_GOLDS;
    copyText(job->key, sizeof(job->key), s_definition->historyKey);
    job->golds = s_golds;
    postJob(job);
}

static void resetRun()
{
    s_runState = RUN_IDLE;
    s_currentSplit = 0;
    memset(s_runSplitsUs, 0, sizeof(s_runSplitsUs));
    s_startTick = s_finishTick = s_loadStartTick = s_pausedTicks = 0;
    s_timerLoading = false;
    s_prevStartCondition = false;
    s_prevEndCondition = false;
#ifdef TPHD_TOOLS_DEBUG
    s_splitDebugFrames = 0;
    s_lastSplitTraceHash = 0;
    s_lastTracedSplit = -1;
#endif
}

static void adoptResults()
{
    OSMessage message;
    while (OSReceiveMessage(&s_resultQueue, &message, OS_MESSAGE_FLAGS_NONE)) {
        Result* result = (Result*)message.message;
        if (!result)
            continue;
        if (result->op == JOB_SCAN) {
            s_scanBusy = false;
            if (!result->error && result->files) {
                free(s_files);
                s_files = result->files;
                result->files = nullptr;
                if (s_files->count == 0) {
                    snprintf(s_status, sizeof(s_status),
                             "No JSON files in tphd/slipts or tphd_tools/splits");
                } else {
                    const Storage::SplitFileEntry* chosen = findFile(s_selectedPath);
                    if (!chosen) {
                        chosen = &s_files->entries[0];
                        copyText(s_selectedPath, sizeof(s_selectedPath), chosen->path);
                    }
                    requestLoad(chosen);
                }
            } else {
                snprintf(s_status, sizeof(s_status), "Split-file scan failed");
            }
        } else if (result->op == JOB_LOAD) {
            s_loadBusy = false;
            if (!result->error && result->definition) {
                free(s_definition);
                s_definition = result->definition;
                result->definition = nullptr;
                s_history = result->history;
                s_golds = result->golds;
                resetRun();
                s_haveLoadingSample = false;
                s_tpStartState = 0;
                s_ignoreCurrentLoad = false;
                s_lastTitleSceneLoad = false;
                snprintf(s_status, sizeof(s_status), "%s loaded (%d splits)",
                         s_definition->displayName, s_definition->splitCount);
                Logger::Log("[tphd_tools] autosplitter loaded: %s, relocation=%08x, splits=%d",
                            s_definition->displayName,
                            (unsigned)s_definition->relocation,
                            s_definition->splitCount);
            } else {
                snprintf(s_status, sizeof(s_status),
                         result->error == 1 ? "Split file disappeared"
                                            : "Split JSON is invalid (see log)");
            }
        } else if (result->op == JOB_SAVE_HISTORY && result->error) {
            Logger::LogError("[tphd_tools] autosplitter history save failed");
        } else if (result->op == JOB_SAVE_GOLDS && result->error) {
            Logger::LogError("[tphd_tools] autosplitter gold save failed");
        }
        free(result->files);
        free(result->definition);
        free(result);
    }
}

enum EvaluationError {
    EVAL_OK = 0,
    EVAL_DISABLED,
    EVAL_POINTER_SOURCE_INVALID,
    EVAL_POINTER_TARGET_INVALID,
    EVAL_POINTER_OVERFLOW,
    EVAL_VALUE_ADDRESS_INVALID,
    EVAL_POINTER_DURING_LOAD,
};

struct ConditionTrace {
    bool passed;
    uint8_t error;
    uint8_t hopCount;
    uint8_t _pad;
    uint32_t baseAddress;
    uint32_t resolvedAddress;
    uint32_t pointerValues[kMaxOffsets];
    uint32_t actualInteger;
    float actualFloat;
    char actualString[kMaxString];
};

struct ConditionSetTrace {
    bool passed;
    uint8_t count;
    uint8_t _pad[2];
    ConditionTrace conditions[kMaxConditions];
};

static bool resolveAddress(const Condition& condition, uint32_t* outAddress,
                           ConditionTrace* trace = nullptr)
{
    uint32_t address = condition.address;
    if (trace)
        trace->baseAddress = address;
    if (condition.usePointer) {
        for (int i = 0; i < condition.offsetCount; ++i) {
            if (!validRange(address, 4)) {
                if (trace) {
                    trace->error = EVAL_POINTER_SOURCE_INVALID;
                    trace->resolvedAddress = address;
                }
                return false;
            }
            uint32_t pointer = *(volatile uint32_t*)address;
            if (trace) {
                trace->pointerValues[i] = pointer;
                trace->hopCount = (uint8_t)(i + 1);
            }
            if (!validRange(pointer, 1)) {
                if (trace) {
                    trace->error = EVAL_POINTER_TARGET_INVALID;
                    trace->resolvedAddress = pointer;
                }
                return false;
            }
            address = pointer + condition.offsets[i];
            if (address < pointer) {
                if (trace) {
                    trace->error = EVAL_POINTER_OVERFLOW;
                    trace->resolvedAddress = address;
                }
                return false;
            }
        }
    }
    if (trace)
        trace->resolvedAddress = address;
    *outAddress = address;
    return true;
}

static bool evaluateCondition(const Condition& condition, ConditionTrace* trace = nullptr,
                              bool allowPointers = true)
{
    if (trace)
        memset(trace, 0, sizeof(*trace));
    if (!condition.enabled) {
        if (trace)
            trace->error = EVAL_DISABLED;
        return false;
    }
    if (condition.usePointer && !allowPointers) {
        if (trace) {
            trace->error = EVAL_POINTER_DURING_LOAD;
            trace->baseAddress = condition.address;
        }
        return false;
    }
    uint32_t address;
    if (!resolveAddress(condition, &address, trace))
        return false;
    if (condition.addressType == 4) {
        size_t len = strlen(condition.stringValue);
        if (!validRange(address, (uint32_t)len + 1)) {
            if (trace)
                trace->error = EVAL_VALUE_ADDRESS_INVALID;
            return false;
        }
        int cmp = strncmp((const char*)address, condition.stringValue, len + 1);
        bool passed = condition.comparisonType == 0 ? cmp == 0 : cmp != 0;
        if (trace) {
            size_t copyLen = len < sizeof(trace->actualString) - 1
                                 ? len
                                 : sizeof(trace->actualString) - 1;
            memcpy(trace->actualString, (const void*)address, copyLen);
            trace->actualString[copyLen] = '\0';
            trace->passed = passed;
        }
        return passed;
    }

    uint32_t actual = 0;
    if (condition.addressType == 0) {
        if (!validRange(address, 1)) {
            if (trace) trace->error = EVAL_VALUE_ADDRESS_INVALID;
            return false;
        }
        actual = *(volatile uint8_t*)address;
    } else if (condition.addressType == 1) {
        if (!validRange(address, 2)) {
            if (trace) trace->error = EVAL_VALUE_ADDRESS_INVALID;
            return false;
        }
        actual = *(volatile uint16_t*)address;
    } else if (condition.addressType == 2) {
        if (!validRange(address, 4)) {
            if (trace) trace->error = EVAL_VALUE_ADDRESS_INVALID;
            return false;
        }
        actual = *(volatile uint32_t*)address;
    } else if (condition.addressType == 3) {
        if (!validRange(address, 4)) {
            if (trace) trace->error = EVAL_VALUE_ADDRESS_INVALID;
            return false;
        }
        float actualFloat = *(volatile float*)address;
        bool passed = false;
        switch (condition.comparisonType) {
        case 0: passed = actualFloat == condition.floatValue; break;
        case 1: passed = actualFloat != condition.floatValue; break;
        case 2: passed = actualFloat >  condition.floatValue; break;
        case 3: passed = actualFloat <  condition.floatValue; break;
        case 4: passed = actualFloat >= condition.floatValue; break;
        case 5: passed = actualFloat <= condition.floatValue; break;
        default: passed = false; break;
        }
        if (trace) {
            trace->actualFloat = actualFloat;
            trace->passed = passed;
        }
        return passed;
    }
    bool passed = false;
    if (condition.comparisonType == 6)
        passed = (actual & (1u << condition.integerValue)) != 0;
    else if (condition.comparisonType == 7)
        passed = (actual & (1u << condition.integerValue)) == 0;
    else
    switch (condition.comparisonType) {
    case 0: passed = actual == condition.integerValue; break;
    case 1: passed = actual != condition.integerValue; break;
    case 2: passed = actual >  condition.integerValue; break;
    case 3: passed = actual <  condition.integerValue; break;
    case 4: passed = actual >= condition.integerValue; break;
    case 5: passed = actual <= condition.integerValue; break;
    default: passed = false; break;
    }
    if (trace) {
        trace->actualInteger = actual;
        trace->passed = passed;
    }
    return passed;
}

static bool evaluateSet(const ConditionSet& set, ConditionSetTrace* trace = nullptr,
                        bool allowPointers = true)
{
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        trace->count = set.count;
    }
    if (set.disabled || set.count == 0)
        return false;
    bool passed = true;
    for (int i = 0; i < set.count; ++i) {
        ConditionTrace* conditionTrace = trace ? &trace->conditions[i] : nullptr;
        if (!evaluateCondition(set.conditions[i], conditionTrace, allowPointers))
            passed = false;
    }
    if (trace)
        trace->passed = passed;
    return passed;
}

#ifdef TPHD_TOOLS_DEBUG
static uint32_t hashTrace(const ConditionSetTrace& trace)
{
    const uint8_t* bytes = (const uint8_t*)&trace;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(trace); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static const char* evaluationErrorName(uint8_t error)
{
    switch (error) {
    case EVAL_OK:                     return "ok";
    case EVAL_DISABLED:               return "disabled";
    case EVAL_POINTER_SOURCE_INVALID: return "pointer-source";
    case EVAL_POINTER_TARGET_INVALID: return "pointer-target";
    case EVAL_POINTER_OVERFLOW:       return "pointer-overflow";
    case EVAL_VALUE_ADDRESS_INVALID:  return "value-address";
    case EVAL_POINTER_DURING_LOAD:    return "load-teardown";
    default:                          return "unknown";
    }
}

static void traceActiveSplit(const Split& split, const ConditionSetTrace& trace,
                             bool force)
{
    uint32_t hash = hashTrace(trace);
    bool changed = s_lastTracedSplit != s_currentSplit ||
                   s_lastSplitTraceHash != hash;
    if (!changed && !force)
        return;

    TPHD_BREADCRUMB(
        "[tphd_tools][autosplit-cond] split=%d/%d name='%s' pass=%u conditions=%u",
        s_currentSplit + 1, s_definition ? s_definition->splitCount : 0,
        split.name, trace.passed ? 1u : 0u, (unsigned)trace.count);

    for (int i = 0; i < trace.count; ++i) {
        const Condition& condition = split.trigger.conditions[i];
        const ConditionTrace& value = trace.conditions[i];
        uint32_t pointer0 = value.hopCount > 0 ? value.pointerValues[0] : 0;
        uint32_t pointer1 = value.hopCount > 1 ? value.pointerValues[1] : 0;
        uint32_t offset0 = condition.offsetCount > 0 ? condition.offsets[0] : 0;
        uint32_t offset1 = condition.offsetCount > 1 ? condition.offsets[1] : 0;

        if (value.error != EVAL_OK) {
            TPHD_BREADCRUMB(
                "[tphd_tools][autosplit-cond] c%d pass=0 error=%s ptr=%u "
                "type=%u cmp=%u base=%08X resolved=%08X "
                "hop0=%08X+%X hop1=%08X+%X",
                i, evaluationErrorName(value.error),
                condition.usePointer ? 1u : 0u,
                (unsigned)condition.addressType,
                (unsigned)condition.comparisonType,
                (unsigned)value.baseAddress, (unsigned)value.resolvedAddress,
                (unsigned)pointer0, (unsigned)offset0,
                (unsigned)pointer1, (unsigned)offset1);
        } else if (condition.addressType == 4) {
            TPHD_BREADCRUMB(
                "[tphd_tools][autosplit-cond] c%d pass=%u ptr=%u type=string "
                "cmp=%u base=%08X resolved=%08X hop0=%08X+%X hop1=%08X+%X "
                "actual='%s' expected='%s'",
                i, value.passed ? 1u : 0u, condition.usePointer ? 1u : 0u,
                (unsigned)condition.comparisonType,
                (unsigned)value.baseAddress, (unsigned)value.resolvedAddress,
                (unsigned)pointer0, (unsigned)offset0,
                (unsigned)pointer1, (unsigned)offset1,
                value.actualString, condition.stringValue);
        } else if (condition.addressType == 3) {
            TPHD_BREADCRUMB(
                "[tphd_tools][autosplit-cond] c%d pass=%u ptr=%u type=float "
                "cmp=%u base=%08X resolved=%08X hop0=%08X+%X hop1=%08X+%X "
                "actual=%f expected=%f",
                i, value.passed ? 1u : 0u, condition.usePointer ? 1u : 0u,
                (unsigned)condition.comparisonType,
                (unsigned)value.baseAddress, (unsigned)value.resolvedAddress,
                (unsigned)pointer0, (unsigned)offset0,
                (unsigned)pointer1, (unsigned)offset1,
                (double)value.actualFloat, (double)condition.floatValue);
        } else {
            TPHD_BREADCRUMB(
                "[tphd_tools][autosplit-cond] c%d pass=%u ptr=%u type=u%u "
                "cmp=%u base=%08X resolved=%08X hop0=%08X+%X hop1=%08X+%X "
                "actual=%u(0x%X) expected=%u(0x%X)",
                i, value.passed ? 1u : 0u, condition.usePointer ? 1u : 0u,
                condition.addressType == 0 ? 8u :
                condition.addressType == 1 ? 16u : 32u,
                (unsigned)condition.comparisonType,
                (unsigned)value.baseAddress, (unsigned)value.resolvedAddress,
                (unsigned)pointer0, (unsigned)offset0,
                (unsigned)pointer1, (unsigned)offset1,
                (unsigned)value.actualInteger, (unsigned)value.actualInteger,
                (unsigned)condition.integerValue, (unsigned)condition.integerValue);
        }
    }

    s_lastTracedSplit = s_currentSplit;
    s_lastSplitTraceHash = hash;
}
#endif

static uint64_t ticksToUs(OSTime ticks)
{
    return OSTicksToMicroseconds((uint64_t)ticks);
}

static uint64_t elapsedUsAt(OSTime now)
{
    if (s_runState == RUN_IDLE || !s_startTick)
        return 0;
    if (s_runState == RUN_FINISHED)
        now = s_finishTick;
    OSTime paused = s_pausedTicks;
    if (s_timerLoading && now > s_loadStartTick)
        paused += now - s_loadStartTick;
    return now > s_startTick + paused ? ticksToUs(now - s_startTick - paused) : 0;
}

static void beginRun()
{
    if (!s_definition)
        return;
    resetRun();
    s_runState = RUN_RUNNING;
    s_startTick = OSGetTime();
    Logger::Log("[tphd_tools] autosplitter run started: %s",
                s_definition->displayName);
}

static void updateHistory()
{
    if (!s_definition || s_currentSplit != s_definition->splitCount)
        return;
    s_history.splitCount = s_definition->splitCount;
    s_golds.splitCount = s_definition->splitCount;
    uint64_t previous = 0;
    for (int i = 0; i < s_definition->splitCount; ++i) {
        uint64_t segment = s_runSplitsUs[i] - previous;
        if (!s_golds.segmentUs[i] || segment < s_golds.segmentUs[i]) {
            s_golds.segmentUs[i] = segment;
        }
        s_history.bestSegmentsUs[i] = s_golds.segmentUs[i];
        previous = s_runSplitsUs[i];
    }
    uint64_t total = s_runSplitsUs[s_definition->splitCount - 1];
    if (!s_history.personalBestUs || total < s_history.personalBestUs) {
        s_history.personalBestUs = total;
        memcpy(s_history.pbSplitsUs, s_runSplitsUs,
               sizeof(uint64_t) * s_definition->splitCount);
    }
    queueHistorySave();
    queueGoldsSave();
}

static void recordSplit(bool finish)
{
    if (s_runState != RUN_RUNNING || !s_definition ||
        s_currentSplit >= s_definition->splitCount)
        return;
    OSTime now = OSGetTime();
    s_runSplitsUs[s_currentSplit] = elapsedUsAt(now);
    Logger::Log("[tphd_tools] autosplitter split %d/%d: %s @ %llu us",
                s_currentSplit + 1, s_definition->splitCount,
                s_definition->splits[s_currentSplit].name,
                (unsigned long long)s_runSplitsUs[s_currentSplit]);
    TPHD_BREADCRUMB(
        "[tphd_tools][autosplit-cond] ADVANCE split=%d/%d name='%s' finish=%u time=%llu",
        s_currentSplit + 1, s_definition->splitCount,
        s_definition->splits[s_currentSplit].name, finish ? 1u : 0u,
        (unsigned long long)s_runSplitsUs[s_currentSplit]);
    ++s_currentSplit;
    if (finish || s_currentSplit >= s_definition->splitCount) {
        s_finishTick = now;
        s_runState = RUN_FINISHED;
        s_timerLoading = false;
        updateHistory();
    }
}

static uint32_t readU32(uint32_t address)
{
    return validRange(address, 4) ? *(volatile uint32_t*)address : 0;
}

static uint8_t readU8(uint32_t address)
{
    return validRange(address, 1) ? *(volatile uint8_t*)address : 0;
}

static bool rawTphdLoading()
{
    return s_definition && s_definition->tp.isLoading &&
           readU32(s_definition->tp.isLoading) == 1;
}

static void setTpStartState(int state, const char* reason)
{
    if (s_tpStartState == state)
        return;
    Logger::Log("[tphd_tools] autosplitter start detector: %d -> %d (%s)",
                s_tpStartState, state, reason ? reason : "");
    TPHD_BREADCRUMB("[tphd_tools][autosplit] detector %d -> %d (%s)",
                    s_tpStartState, state, reason ? reason : "");
    s_tpStartState = state;
}

static dFile_select_c* activeFileSelect(dScnName_c** outScene = nullptr,
                                        dFile_select_c** outRaw = nullptr)
{
    dScnName_c* scene = dScnName_get();
    dFile_select_c* fileSelect = scene ? scene->mFileSelect : nullptr;
    if (outScene)
        *outScene = scene;
    if (outRaw)
        *outRaw = fileSelect;
    uint32_t address = (uint32_t)(uintptr_t)fileSelect;
    // dFile_select_c is allocated from the game's expanded heap. In Cemu it
    // commonly lives above 0x12000000, where coreinit's OSIsAddressValid()
    // reports false even though Zelda owns and dereferences the allocation.
    // The pointer came directly from the live Name Scene, so only reject null,
    // unaligned, wrapped, or non-user-space values here.
    bool sane = address >= 0x10000000u &&
                address <= 0xEFFFFFFFu - 0x2D3u &&
                (address & 3u) == 0;
    return sane ? fileSelect : nullptr;
}

#ifdef TPHD_TOOLS_DEBUG
static void traceStartDetector(bool rawLoading, bool force = false)
{
    dScnName_c* scene = nullptr;
    dFile_select_c* rawFileSelect = nullptr;
    dFile_select_c* fileSelect = activeFileSelect(&scene, &rawFileSelect);

    StartDebugSnapshot now = {};
    now.enabled = s_enabled;
    now.autoStart = s_autoStart;
    now.hasDefinition = s_definition != nullptr;
    now.startDisabled = s_definition && s_definition->start.disabled;
    now.runState = (uint8_t)s_runState;
    now.detectorState = (uint8_t)s_tpStartState;
    now.rawLoading = rawLoading ? 1 : 0;
    now.timestampSet = dSv_hasSaveTimestamp() ? 1 : 0;
    now.nameScene = (uint32_t)(uintptr_t)scene;
    now.rawFileSelect = (uint32_t)(uintptr_t)rawFileSelect;
    now.validFileSelect = (uint32_t)(uintptr_t)fileSelect;
    if (fileSelect) {
        now.selectionPath = fileSelect->mSelectionPath;
        now.fileState = fileSelect->mState;
        now.loadResult = fileSelect->mLoadResult;
        now.selectedSlot = fileSelect->mSelectedSlot;
        now.heroChoice = fileSelect->mHeroModeChoice;
    }

    bool changed = !s_haveStartDebug ||
                   memcmp(&now, &s_lastStartDebug, sizeof(now)) != 0;
    if (changed || force) {
        TPHD_BREADCRUMB(
            "[tphd_tools][autosplit] tick enabled=%u auto=%u def=%u customStart=%u "
            "run=%u detector=%u load=%u scene=%08X rawFS=%08X validFS=%08X "
            "path=%u state=%02X result=%u slot=%u hero=%u timestamp=%u",
            now.enabled ? 1u : 0u, now.autoStart ? 1u : 0u,
            now.hasDefinition ? 1u : 0u, now.startDisabled ? 0u : 1u,
            (unsigned)now.runState, (unsigned)now.detectorState,
            (unsigned)now.rawLoading, (unsigned)now.nameScene,
            (unsigned)now.rawFileSelect, (unsigned)now.validFileSelect,
            (unsigned)now.selectionPath, (unsigned)now.fileState,
            (unsigned)now.loadResult, (unsigned)now.selectedSlot,
            (unsigned)now.heroChoice, (unsigned)now.timestampSet);
        s_lastStartDebug = now;
        s_haveStartDebug = true;
    }
}
#endif

static void updateLoading(bool rawLoading)
{
    bool countAsLoad = false;
    if (s_definition && s_definition->tp.isLoading) {
        if (rawLoading && !s_prevRawLoading) {
            bool titleScene = dScn_isPresent(FPCNM_SCENE_OPENING) ||
                              dScn_isPresent(FPCNM_SCENE_NAME);
            bool continuedTitleLoad = s_lastTitleSceneLoad;
            bool ignore = continuedTitleLoad || titleScene ||
                          readU8(s_definition->tp.nextSpawn) == 0xFF;
            s_ignoreCurrentLoad = ignore;
            // Opening -> Name marks the next load as a file-select load. When
            // that next edge arrives, consume the marker even though the Name
            // Scene may still exist during the first transition frame.
            s_lastTitleSceneLoad = !continuedTitleLoad && titleScene;
        } else if (!rawLoading) {
            s_ignoreCurrentLoad = false;
        }
        countAsLoad = rawLoading && !s_ignoreCurrentLoad;
    } else if (s_definition && !s_definition->loading.disabled) {
        countAsLoad = evaluateSet(s_definition->loading);
    }
    if (!s_removeLoads || s_runState != RUN_RUNNING)
        countAsLoad = false;

    OSTime now = OSGetTime();
    if (countAsLoad && !s_timerLoading) {
        s_timerLoading = true;
        s_loadStartTick = now;
    } else if (!countAsLoad && s_timerLoading) {
        if (now > s_loadStartTick)
            s_pausedTicks += now - s_loadStartTick;
        s_timerLoading = false;
    }
    s_prevRawLoading = rawLoading;
}

static void updateAutoStart()
{
    if (!s_autoStart || !s_definition || s_runState == RUN_RUNNING)
        return;
    if (!s_definition->start.disabled) {
        bool match = evaluateSet(s_definition->start);
        if (match && !s_prevStartCondition)
            beginRun();
        s_prevStartCondition = match;
        return;
    }

    // The external splitter inferred this event by writing a sentinel into the
    // Hero Mode byte. Poll the real dFile_select_c state machine instead:
    // FUN_02940fd4 commits a newly created file and stores state 0x39.
    dFile_select_c* fileSelect = activeFileSelect();
    if (!fileSelect) {
        if (s_tpStartState != 0)
            setTpStartState(0, "Name Scene exited");
        return;
    }

    if (s_tpStartState == 0)
        setTpStartState(1, "Name Scene file-select ready");

    if (fileSelect->mSelectionPath == DFILE_SELECT_PATH_NEW_FILE &&
        fileSelect->mState == DFILE_SELECT_STATE_NEW_FILE_COMMITTED &&
        s_tpStartState < 2) {
        // Path 4 reaches state 0x39 only through FUN_02940fd4, the new-file
        // Hero Mode confirmation/commit routine. Existing-file loads use other
        // states, so the save timestamp is informative but is not a safe gate:
        // the live info block can retain title-session data while the new image
        // is still held in dFile_select_c's private buffer.
        beginRun();
        TPHD_BREADCRUMB(
            "[tphd_tools][autosplit] START path=%u state=%02X result=%u slot=%u hero=%u",
            (unsigned)fileSelect->mSelectionPath, (unsigned)fileSelect->mState,
            (unsigned)fileSelect->mLoadResult, (unsigned)fileSelect->mSelectedSlot,
            (unsigned)fileSelect->mHeroModeChoice);
        setTpStartState(2, "new file committed");
    }
}

static void updateTriggers(bool rawLoading)
{
    if (s_runState != RUN_RUNNING || !s_definition)
        return;
    bool end = evaluateSet(s_definition->end, nullptr, !rawLoading);
    if (end && !s_prevEndCondition) {
        while (s_currentSplit < s_definition->splitCount - 1)
            recordSplit(false);
        if (s_runState == RUN_RUNNING)
            recordSplit(true);
    }
    s_prevEndCondition = end;
    if (s_runState != RUN_RUNNING ||
        s_currentSplit >= s_definition->splitCount - 1)
        return;

#ifdef TPHD_TOOLS_DEBUG
    ConditionSetTrace splitTrace;
    bool splitMatched =
        evaluateSet(s_definition->splits[s_currentSplit].trigger, &splitTrace,
                    !rawLoading);
    bool periodicTrace = ++s_splitDebugFrames >= 150;
    if (periodicTrace)
        s_splitDebugFrames = 0;
    traceActiveSplit(s_definition->splits[s_currentSplit], splitTrace, periodicTrace);
#else
    bool splitMatched =
        evaluateSet(s_definition->splits[s_currentSplit].trigger, nullptr,
                    !rawLoading);
#endif
    if (splitMatched)
        recordSplit(false);
}

void Initialize()
{
    if (s_initialized)
        return;
    s_initialized = true;
    startWorker();
    requestScan();
}

void Tick()
{
    if (!s_initialized)
        Initialize();
    adoptResults();
#ifdef TPHD_TOOLS_DEBUG
    bool debugRawLoading = rawTphdLoading();
    traceStartDetector(debugRawLoading);
#endif
    if (!s_enabled || !s_definition)
        return;
    bool rawLoading = rawTphdLoading();
    if (!s_haveLoadingSample) {
        s_prevRawLoading = rawLoading;
        s_haveLoadingSample = true;
    } else {
        updateAutoStart();
    }
    updateLoading(rawLoading);
    updateTriggers(rawLoading);
}

static void formatTime(uint64_t us, char* out, int outSize, bool hundredths)
{
    uint64_t totalCs = us / 10000;
    uint64_t hours = totalCs / 360000;
    uint64_t minutes = (totalCs / 6000) % 60;
    uint64_t seconds = (totalCs / 100) % 60;
    uint64_t cs = totalCs % 100;
    if (hours)
        snprintf(out, outSize, hundredths ? "%llu:%02llu:%02llu.%02llu"
                                         : "%llu:%02llu:%02llu",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds, (unsigned long long)cs);
    else
        snprintf(out, outSize, hundredths ? "%llu:%02llu.%02llu"
                                         : "%llu:%02llu",
                 (unsigned long long)minutes, (unsigned long long)seconds,
                 (unsigned long long)cs);
}

static void formatDelta(int64_t us, char* out, int outSize)
{
    uint64_t absUs = (uint64_t)(us < 0 ? -us : us);
    uint64_t tenths = (absUs + 50000) / 100000;
    if (tenths < 600) {
        snprintf(out, outSize, "%c%llu.%llu", us < 0 ? '-' : '+',
                 (unsigned long long)(tenths / 10),
                 (unsigned long long)(tenths % 10));
        return;
    }

    uint64_t totalSeconds = (absUs + 500000) / 1000000;
    uint64_t hours = totalSeconds / 3600;
    uint64_t minutes = (totalSeconds / 60) % 60;
    uint64_t seconds = totalSeconds % 60;
    if (hours) {
        snprintf(out, outSize, "%c%llu:%02llu:%02llu", us < 0 ? '-' : '+',
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
    } else {
        snprintf(out, outSize, "%c%llu:%02llu", us < 0 ? '-' : '+',
                 (unsigned long long)minutes, (unsigned long long)seconds);
    }
}

static uint64_t deltaPreviewUs()
{
    float seconds = s_deltaPreviewSeconds;
    if (seconds < 0.0f)
        seconds = 0.0f;
    if (seconds > 600.0f)
        seconds = 600.0f;
    return (uint64_t)(seconds * 1000000.0f + 0.5f);
}

static bool shouldShowDelta(uint64_t shown, uint64_t pb, bool complete, bool active)
{
    if (!shown || !pb)
        return false;
    if (complete)
        return true;
    if (!active)
        return false;
    uint64_t threshold = deltaPreviewUs();
    return shown >= pb || pb - shown <= threshold;
}

static void splitInitials(const char* name, char* out, int outSize)
{
    if (!out || outSize <= 0)
        return;
    out[0] = '\0';
    if (!name || !name[0])
        return;

    int n = 0;
    bool wordStart = true;
    for (const char* p = name; *p && n < outSize - 1; ++p) {
        char c = *p;
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool digit = c >= '0' && c <= '9';
        if ((alpha || digit) && wordStart) {
            out[n++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            wordStart = false;
        } else if (!alpha && !digit) {
            wordStart = true;
        }
    }
    if (!n) {
        out[n++] = name[0];
    }
    out[n] = '\0';
}

static uint64_t bestPossible()
{
    if (!s_definition)
        return 0;
    uint64_t sum = 0;
    for (int i = 0; i < s_definition->splitCount; ++i) {
        if (!s_golds.segmentUs[i])
            return 0;
        sum += s_golds.segmentUs[i];
    }
    return sum;
}

void DrawMenuItem()
{
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Auto Splitter", &enabled)) {
        SetEnabled(enabled);
        if (s_enabled && !s_files)
            requestScan();
    }
    if (s_enabled) {
        ImGui::Indent();
        ImGui::TextDisabled("%s", s_status);
        ImGui::TextDisabled("Settings live in the Auto Splitter window.");
        ImGui::Unindent();
    }
}

static void drawFilePicker()
{
    const char* preview = s_definition ? s_definition->displayName : "Select split file";
    ImGui::SetNextItemWidth(315.0f);
    if (ImGui::BeginCombo("Split file##autosplitter", preview)) {
        if (s_files)
            for (int i = 0; i < s_files->count; ++i) {
                const Storage::SplitFileEntry& file = s_files->entries[i];
                bool selected = strcmp(file.path, s_selectedPath) == 0;
                if (ImGui::Selectable(file.name, selected) && !s_loadBusy) {
                    copyText(s_selectedPath, sizeof(s_selectedPath), file.path);
                    requestLoad(&file);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        ImGui::EndCombo();
    }
}

static void drawRunControls()
{
    if (ImGui::Button("Rescan"))
        requestScan();
    ImGui::SameLine();
    if (s_runState == RUN_RUNNING) {
        if (ImGui::Button("Split"))
            recordSplit(s_currentSplit >= (s_definition ? s_definition->splitCount - 1 : 0));
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            resetRun();
    } else {
        if (ImGui::Button("Start"))
            beginRun();
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            resetRun();
    }
}

static void drawSettingsTab()
{
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        SetEnabled(enabled);
        if (s_enabled && !s_files)
            requestScan();
    }
    ImGui::TextDisabled("%s", s_status);

    ImGui::Separator();

    bool autoStart = s_autoStart;
    if (ImGui::Checkbox("Auto-start new files", &autoStart))
        SetAutoStartEnabled(autoStart);

    bool removeLoads = s_removeLoads;
    if (ImGui::Checkbox("Remove gameplay loads", &removeLoads))
        SetLoadRemovalEnabled(removeLoads);

    float previewSeconds = s_deltaPreviewSeconds;
    if (ImGui::SliderFloat("Show +/- within", &previewSeconds, 0.0f, 120.0f,
                           "%.1f sec"))
        SetDeltaPreviewSeconds(previewSeconds);
    ImGui::TextDisabled("For the active split, +/- appears this close to the PB time.");

    bool initials = s_initialsWhenDeltaShown;
    if (ImGui::Checkbox("Use initials while +/- is visible", &initials))
        SetInitialsWhenDeltaShownEnabled(initials);

    ImGui::Separator();

    drawFilePicker();
    drawRunControls();

    if (s_definition) {
        ImGui::TextDisabled("Gold file: tphd_tools/split_golds/%s.json",
                            s_definition->historyKey);
    }

    if (s_definition && s_definition->start.disabled) {
        dFile_select_c* fileSelect = activeFileSelect();
        ImGui::TextDisabled(
            "Start detector: state=%d name=%d fs=%08x path=%u select=%02x result=%u saved=%d",
            s_tpStartState, fileSelect ? 1 : 0, (unsigned)(uintptr_t)fileSelect,
            fileSelect ? (unsigned)fileSelect->mSelectionPath : 0,
            fileSelect ? (unsigned)fileSelect->mState : 0,
            fileSelect ? (unsigned)fileSelect->mLoadResult : 0,
            dSv_hasSaveTimestamp() ? 1 : 0);
    }
}

static void drawSummaryLine(const char* label, uint64_t value)
{
    char text[40];
    ImGui::TextUnformatted(label);
    ImGui::SameLine(125.0f);
    if (value) {
        formatTime(value, text, sizeof(text), true);
        ImGui::TextUnformatted(text);
    } else {
        ImGui::TextDisabled("--");
    }
}

static void drawSplitsTab()
{
    if (!s_definition) {
        ImGui::TextWrapped("%s", s_status);
        ImGui::TextDisabled("SD: tphd/slipts/*.json or tphd_tools/splits/*.json");
        return;
    }

    ImGui::TextUnformatted(s_definition->displayName);
    ImGui::SameLine();
    ImGui::TextDisabled("%d/%d", s_currentSplit, s_definition->splitCount);
    ImGui::Separator();

    int first = s_currentSplit > 4 ? s_currentSplit - 4 : 0;
    int last = first + 10;
    if (last > s_definition->splitCount)
        last = s_definition->splitCount;
    if (last - first < 10 && last == s_definition->splitCount) {
        first = last - 10;
        if (first < 0) first = 0;
    }

    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                 ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##split_rows", 4, tableFlags, ImVec2(0.0f, 325.0f))) {
        ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch, 2.25f);
        ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Golds", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableHeadersRow();

        for (int i = first; i < last; ++i) {
            bool complete = i < s_currentSplit;
            bool active = i == s_currentSplit && s_runState == RUN_RUNNING;
            uint64_t shown = 0;
            if (complete)
                shown = s_runSplitsUs[i];
            else if (active)
                shown = elapsedUsAt(OSGetTime());

            uint64_t pb = s_history.pbSplitsUs[i];
            bool showDelta = shouldShowDelta(shown, pb, complete, active);

            char nameText[96];
            if (showDelta && s_initialsWhenDeltaShown)
                splitInitials(s_definition->splits[i].name, nameText, sizeof(nameText));
            else
                copyText(nameText, sizeof(nameText), s_definition->splits[i].name);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (active)
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "> %s",
                                   nameText);
            else
                ImGui::TextUnformatted(nameText);

            char text[40];
            ImGui::TableSetColumnIndex(1);
            if (showDelta) {
                int64_t delta = (int64_t)shown - (int64_t)pb;
                formatDelta(delta, text, sizeof(text));
                ImVec4 color = delta <= 0 ? ImVec4(0.25f, 0.9f, 0.5f, 1.0f)
                                          : ImVec4(1.0f, 0.35f, 0.28f, 1.0f);
                ImGui::TextColored(color, "%s", text);
            }

            ImGui::TableSetColumnIndex(2);
            if (pb) {
                formatTime(pb, text, sizeof(text), false);
                ImGui::TextUnformatted(text);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableSetColumnIndex(3);
            uint64_t gold = s_golds.segmentUs[i];
            if (shown && gold) {
                uint64_t previous = i ? s_runSplitsUs[i - 1] : 0;
                uint64_t segment = shown > previous ? shown - previous : 0;
                if (segment > gold) {
                    formatDelta((int64_t)(segment - gold), text, sizeof(text));
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.28f, 1.0f),
                                       "%s", text);
                }
            }
        }
        ImGui::EndTable();
    }

    uint64_t elapsed = elapsedUsAt(OSGetTime());
    char timer[48];
    formatTime(elapsed, timer, sizeof(timer), true);
    float timerWidth = ImGui::CalcTextSize(timer).x * 1.65f;
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - timerWidth;
    if (x > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(x);
    ImGui::SetWindowFontScale(1.65f);
    ImGui::TextColored(s_runState == RUN_FINISHED
                           ? ImVec4(0.3f, 0.9f, 0.55f, 1.0f)
                           : ImVec4(0.25f, 0.7f, 1.0f, 1.0f),
                       "%s", timer);
    ImGui::SetWindowFontScale(1.0f);
    if (s_timerLoading)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "LOAD REMOVED");
    else
        ImGui::TextDisabled(s_runState == RUN_IDLE ? "Ready" :
                            s_runState == RUN_FINISHED ? "Finished" : "Running");

    ImGui::Separator();
    drawSummaryLine("PB", s_history.personalBestUs);
    drawSummaryLine("World Record", bestPossible());
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowPos(ImVec2(800.0f, 70.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470.0f, 610.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82f * ov::g_settings.overlayOpacity);
    bool open = ImGui::Begin("Auto Splitter", menuActive ? &s_enabled : nullptr, flags);
    if (!open) {
        ImGui::End();
        return;
    }

    if (menuActive) {
        if (ImGui::BeginTabBar("##autosplit_tabs")) {
            if (ImGui::BeginTabItem("Splits")) {
                drawSplitsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                drawSettingsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        drawSplitsTab();
    }
    ImGui::End();
}

bool IsEnabled() { return s_enabled; }
void SetEnabled(bool enabled)
{
    if (s_enabled != enabled)
        TPHD_BREADCRUMB("[tphd_tools][autosplit] enabled %u -> %u",
                        s_enabled ? 1u : 0u, enabled ? 1u : 0u);
    s_enabled = enabled;
}
bool IsAutoStartEnabled() { return s_autoStart; }
void SetAutoStartEnabled(bool enabled)
{
    if (s_autoStart != enabled)
        TPHD_BREADCRUMB("[tphd_tools][autosplit] auto-start %u -> %u",
                        s_autoStart ? 1u : 0u, enabled ? 1u : 0u);
    s_autoStart = enabled;
}
bool IsLoadRemovalEnabled() { return s_removeLoads; }
void SetLoadRemovalEnabled(bool enabled) { s_removeLoads = enabled; }
float GetDeltaPreviewSeconds() { return s_deltaPreviewSeconds; }
void SetDeltaPreviewSeconds(float seconds)
{
    if (seconds < 0.0f)
        seconds = 0.0f;
    if (seconds > 600.0f)
        seconds = 600.0f;
    s_deltaPreviewSeconds = seconds;
}
bool IsInitialsWhenDeltaShownEnabled() { return s_initialsWhenDeltaShown; }
void SetInitialsWhenDeltaShownEnabled(bool enabled)
{
    s_initialsWhenDeltaShown = enabled;
}
const char* GetSelectedPath() { return s_selectedPath; }
void SetSelectedPath(const char* path)
{
    if (!path)
        path = "";
    copyText(s_selectedPath, sizeof(s_selectedPath), path);
}

} // namespace AutoSplitter
} // namespace Tools
