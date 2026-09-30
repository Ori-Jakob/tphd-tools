#include "tools/save_states.h"

#include "core/config.h"
#include "core/controller.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/storage.h"
#include "ui/notifications.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"
#include "ui/ui_window.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_horse.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "d/d_procname.h"
#include "d/d_save.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"
#include "libtphd/addr_v81.h"
#include "libtphd/reloc.h"
#include "m_Do/m_Do_controller_pad.h"

#include "imgui.h"

#include "procs_bin.h"

#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace SaveStates {
static const uint32_t kMagic   = 0x54505353u;
static const uint32_t kVersion = 8u;

struct StateHeader {
    uint32_t magic;
    uint32_t version;
    char     stage[8];
    s8       room;
    s8       layer;
    s16      spawn;
    cXyz     pos;
    s16      angle;
    cXyz     camAt;
    cXyz     camEye;
    cXyz     horsePos;
    s16      horseAngle;
    s8       horseRoom;
    u8       horseRiding;
    u8       camValid;
    u8       _pad[3];
};

static_assert(sizeof(StateHeader) == 80, "StateHeader layout changed");

static const uint32_t kInfoSize     = sizeof(dSv_info_c);
static const uint32_t kBaseFileSize = sizeof(StateHeader) + sizeof(dSv_info_c);

static const uint32_t kCommandMagic    = 0x54504344u;
static const uint16_t kCommandVersion  = 1;
static const uint32_t kMaxCommands     = 128;
static const uint32_t kMaxCommandBytes = 8192;
// Older files never set this bit, so they load with every command enabled.
static const uint32_t kCommandDisabledFlag   = 0x80000000u;
static const uint32_t kCommandFrameDelayMask = 0x7FFFFFFFu;
static const u16      kMaxSpawnAmount        = 64;
// Deleting stage actors still in their async create phase crashes the game.
static const uint32_t kDeleteBaseFrameDelay = 10;

enum CommandType {
    COMMAND_SPAWN_ACTOR   = 1,
    COMMAND_DELETE_ACTORS = 2,
    COMMAND_SET_AREA_FLAG = 3,
};

enum AreaFlagType {
    AREA_FLAG_CHEST = 0,
    AREA_FLAG_SWITCH,
    AREA_FLAG_ITEM,
    AREA_FLAG_DUNGEON_ITEM,
    AREA_FLAG_KEY_COUNT,
};

struct CommandSectionHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t totalSize;
    uint32_t commandCount;
    uint32_t payloadCrc32;
};

struct CommandRecordHeader {
    uint16_t type;
    uint16_t size;
    uint32_t frameDelay;
};

struct SpawnActorPayload {
    s16   procId;
    s8    subtype;
    u8    reserved0;
    u32   params;
    cXyz  position;
    csXyz angle;
    u16   amount;
};

struct DeleteActorsPayload {
    s16 procId;
    u16 reserved;
};

struct SetAreaFlagPayload {
    u8  flagType;
    u8  value;
    u16 index;
};

struct StateCommand {
    uint16_t type;
    uint16_t flagType;
    uint32_t frameDelay;
    s16      procId;
    s8       subtype;
    u8       flagValue;
    u32      params;
    cXyz     position;
    csXyz    angle;
    u16      flagIndex;
    u16      amount;
    u8       enabled;
};

static_assert(sizeof(CommandSectionHeader) == 20, "CommandSectionHeader layout changed");
static_assert(sizeof(CommandRecordHeader) == 8, "CommandRecordHeader layout changed");
static_assert(sizeof(SpawnActorPayload) == 28, "SpawnActorPayload layout changed");
static_assert(sizeof(DeleteActorsPayload) == 4, "DeleteActorsPayload layout changed");
static_assert(sizeof(SetAreaFlagPayload) == 4, "SetAreaFlagPayload layout changed");

static const uint32_t kMaxFileSize =
    kBaseFileSize + sizeof(CommandSectionHeader) + kMaxCommandBytes;
static const uint32_t kSpawnRecordSize =
    sizeof(CommandRecordHeader) + sizeof(SpawnActorPayload);
static const uint32_t kDeleteRecordSize =
    sizeof(CommandRecordHeader) + sizeof(DeleteActorsPayload);
static const uint32_t kAreaFlagRecordSize =
    sizeof(CommandRecordHeader) + sizeof(SetAreaFlagPayload);

static const uint32_t kProcRecordSize  = 64u;
static const uint32_t kProcNameOff     = 2u;
static const uint32_t kProcFriendlyOff = 32u;

typedef void (*dSv_info_setSwitch_t)(dSv_info_c* info, int flag, int roomNo);
static const uint32_t kOnSwitchAddr  = 0x02AA8714u;
static const uint32_t kOffSwitchAddr = 0x02AA8780u;

enum { OV_IDLE = 0, OV_TEARDOWN, OV_WAIT, OV_APPLY };

static const int kPhase1Timeout       = 300;
static const int kWaitTimeout         = 300;
static const int kHoldTimeout         = 90;
static const int kGroundConfirm       = 3;
static const int kReadyConfirm        = 3;
static const int kHorseSpawnGrace     = 30;
// Mounted Link never touches the ground; hold Epona past the wipe and the room-entry snap.
static const int kHorseHoldFrames     = 30;
static const int kControllerReselect  = 150;
static const u16 kFullOxygen          = 600;

static const char kWindowName[]       = "Save Loader";
static const char kEditorWindowName[] = "Save State Commands";
static const char kTitle[]            = "Save Loader";
static const char kNotifyKey[]        = "savestate";
static const char kStateDir[]         = "savestates";
static const int  kNameMax            = 64;
static const int  kMaxFolders         = 5;
static const int  kMaxFiles           = 64;

alignas(64) static uint8_t s_fileBuf[kMaxFileSize];
alignas(64) static uint8_t s_pendingStorage[kInfoSize];
static uint8_t*             s_pendingInfo = nullptr;

static StateHeader          s_loadHeader;
static bool                 s_loadReady = false;
static char                 s_readyFolder[kNameMax] = "";
static char                 s_readyName[kNameMax] = "";
static bool                 s_applyPosNext = true;
static bool                 s_pendingSerialized = false;
static bool                 s_normalSaveResume = false;
static StateCommand         s_loadCommands[kMaxCommands];
static uint32_t             s_loadCommandCount = 0;

static int      s_ovPhase = OV_IDLE;
static int      s_ovWait = 0;
static int      s_ovApply = 0;
static int      s_ovGround = 0;
static char     s_ovStage[8] = {};
static char     s_ovLabel[132] = "";
static cXyz     s_ovPos = {};
static s16      s_ovAngle = 0;
static s8       s_ovRoom = 0;
static cXyz     s_ovCamAt = {};
static cXyz     s_ovCamEye = {};
static bool     s_ovCamValid = false;
static cXyz     s_ovHorsePos = {};
static s16      s_ovHorseAngle = 0;
static s8       s_ovHorseRoom = 0;
static bool     s_ovHorseRiding = false;
static bool     s_ovHorseSpawnRequested = false;
static int      s_ovHorseWait = 0;
static int      s_ovReadyFrames = 0;
static bool     s_ovApplyPos = true;
static bool     s_ovCommandsStarted = false;
static bool     s_ovEarlyStageFlagsRestored = false;
static uint32_t s_ovEarlyZoneRoomMask[2] = {};
static int      s_ctrlReselect = 0;

static StateCommand s_execCommands[kMaxCommands];
static uint32_t     s_execCommandCount = 0;
static uint32_t     s_execCommandIndex = 0;
static uint32_t     s_execFrame = 0;
static char         s_execStage[8] = {};

static bool s_phase1Claimed = false;
static bool s_zoneClaimed = false;
static bool s_hooksInstalled = false;
static bool s_hookWarned = false;

static bool s_open = false;
static bool s_overridePosition = true;
static bool s_reloadHotkey = false;
static char s_lastFolder[kNameMax] = "";
static char s_lastName[kNameMax] = "";
static char s_nameBuf[kNameMax] = "";
static char s_status[128] = "";

static char s_folders[kMaxFolders][kNameMax];
static int  s_folderCount = 0;
static bool s_foldersLoaded = false;
static int  s_selectedTab = 0;
static char s_files[kMaxFiles][kNameMax];
static int  s_fileCount = 0;
static bool s_listLoaded = false;
static int  s_confirmDelete = -1;
static bool s_openDeletePopup = false;

static bool         s_editorOpen = false;
static bool         s_editorLoaded = false;
alignas(64) static uint8_t s_editorBase[kBaseFileSize];
static StateCommand s_editorCommands[kMaxCommands];
static uint32_t     s_editorCommandCount = 0;
static char         s_editorFolder[kNameMax] = "";
static char         s_editorName[kNameMax] = "";
static StateCommand s_editorForm = {};
static int          s_editorFormIndex = -1;
static char         s_editorQuery[64] = "";
static char         s_editorStatus[128] = "";

static void setStatus(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static void setEditorStatus(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_editorStatus, sizeof(s_editorStatus), fmt, ap);
    va_end(ap);
}

static void fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, kTitle, s_status);
}

static void copyName(char* out, size_t size, const char* src)
{
    size_t i = 0;
    for (; src && i + 1 < size && src[i]; ++i)
        out[i] = src[i];
    out[i] = '\0';
}

static void locationText(char* out, size_t size, const char* folder, const char* name)
{
    if (folder && folder[0])
        snprintf(out, size, "%s/%s", folder, name);
    else
        snprintf(out, size, "%s", name);
}

static void sanitizeImage(uint8_t* image)
{
    // The deserializer uses this byte as an unchecked index into a 4-entry table.
    if (image[dSv_IMAGE_CAMERA_CONFIG_OFF] > dSv_IMAGE_CAMERA_CONFIG_MAX)
        image[dSv_IMAGE_CAMERA_CONFIG_OFF] = dSv_IMAGE_CAMERA_CONFIG_MAX;
}

static uint32_t crc32(const uint8_t* data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool actorProcIdValid(s16 procId)
{
    if (procId < 0 || (uint32_t)procId >= procs_bin_size / kProcRecordSize)
        return false;
    const uint8_t* record = procs_bin + (uint32_t)procId * kProcRecordSize;
    return (((uint16_t)record[0] << 8) | record[1]) == (uint16_t)procId;
}

static const char* actorProcName(s16 procId)
{
    return actorProcIdValid(procId)
               ? (const char*)(procs_bin + (uint32_t)procId * kProcRecordSize + kProcNameOff)
               : "?";
}

static const char* actorFriendlyName(s16 procId)
{
    return actorProcIdValid(procId)
               ? (const char*)(procs_bin + (uint32_t)procId * kProcRecordSize +
                               kProcFriendlyOff)
               : "";
}

static const char* actorDisplayName(s16 procId)
{
    const char* friendly = actorFriendlyName(procId);
    return friendly[0] ? friendly : actorProcName(procId);
}

static bool containsIgnoreCase(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0])
        return true;
    for (const char* start = text; *start; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

static bool commandSemanticsValid(const StateCommand& command)
{
    if (command.enabled > 1)
        return false;
    if (command.type == COMMAND_SPAWN_ACTOR)
        return actorProcIdValid(command.procId) && command.amount >= 1 &&
               command.amount <= kMaxSpawnAmount;
    if (command.type == COMMAND_DELETE_ACTORS)
        return actorProcIdValid(command.procId) && command.procId != fpcNm_PLACEHOLDER_LINK;
    if (command.type != COMMAND_SET_AREA_FLAG || command.flagType > AREA_FLAG_KEY_COUNT)
        return false;

    switch (command.flagType) {
    case AREA_FLAG_CHEST:        return command.flagIndex < 64 && command.flagValue <= 1;
    case AREA_FLAG_SWITCH:       return command.flagIndex < 128 && command.flagValue <= 1;
    case AREA_FLAG_ITEM:         return command.flagIndex < 32 && command.flagValue <= 1;
    case AREA_FLAG_DUNGEON_ITEM: return command.flagIndex < 8 && command.flagValue <= 1;
    case AREA_FLAG_KEY_COUNT:    return command.flagIndex <= 255;
    default:                     return false;
    }
}

static bool finite3(const cXyz& v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool parseStateImage(const uint8_t* image, uint32_t size, StateCommand* outCommands,
                            uint32_t* outCommandCount)
{
    *outCommandCount = 0;
    if (!image || size < kBaseFileSize || size > kMaxFileSize)
        return false;

    const StateHeader* state = (const StateHeader*)image;
    if (state->magic != kMagic || state->version != kVersion)
        return false;
    if (!state->stage[0] || !memchr(state->stage, '\0', sizeof(state->stage)) ||
        state->horseRiding > 1 || state->camValid > 1 || !finite3(state->pos))
        return false;
    if (state->camValid && (!finite3(state->camAt) || !finite3(state->camEye)))
        return false;
    if (state->horseRiding && !finite3(state->horsePos))
        return false;
    if (size == kBaseFileSize)
        return true;
    if (size < kBaseFileSize + sizeof(CommandSectionHeader))
        return false;

    const CommandSectionHeader* section = (const CommandSectionHeader*)(image + kBaseFileSize);
    if (section->magic != kCommandMagic || section->version != kCommandVersion ||
        section->headerSize != sizeof(CommandSectionHeader) ||
        section->commandCount > kMaxCommands || section->totalSize != size - kBaseFileSize ||
        section->totalSize < sizeof(CommandSectionHeader))
        return false;

    const uint32_t payloadSize = section->totalSize - sizeof(CommandSectionHeader);
    const uint8_t* payload = image + kBaseFileSize + sizeof(CommandSectionHeader);
    if (crc32(payload, payloadSize) != section->payloadCrc32)
        return false;

    uint32_t offset = kBaseFileSize + sizeof(CommandSectionHeader);
    for (uint32_t i = 0; i < section->commandCount; ++i) {
        if (offset > size || size - offset < sizeof(CommandRecordHeader))
            return false;
        const CommandRecordHeader* record = (const CommandRecordHeader*)(image + offset);
        if (record->size < sizeof(CommandRecordHeader) || record->size > size - offset)
            return false;

        StateCommand command = {};
        command.type = record->type;
        command.frameDelay = record->frameDelay & kCommandFrameDelayMask;
        command.enabled = (record->frameDelay & kCommandDisabledFlag) ? 0 : 1;
        command.amount = 1;
        const uint8_t* recordPayload = image + offset + sizeof(*record);

        if (record->type == COMMAND_SPAWN_ACTOR && record->size == kSpawnRecordSize) {
            const SpawnActorPayload* spawn = (const SpawnActorPayload*)recordPayload;
            if (spawn->reserved0)
                return false;
            command.procId = spawn->procId;
            command.subtype = spawn->subtype;
            command.params = spawn->params;
            command.position = spawn->position;
            command.angle = spawn->angle;
            command.amount = spawn->amount ? spawn->amount : 1;
            if (!finite3(command.position))
                return false;
        } else if (record->type == COMMAND_DELETE_ACTORS && record->size == kDeleteRecordSize) {
            const DeleteActorsPayload* remove = (const DeleteActorsPayload*)recordPayload;
            if (remove->reserved)
                return false;
            command.procId = remove->procId;
        } else if (record->type == COMMAND_SET_AREA_FLAG &&
                   record->size == kAreaFlagRecordSize) {
            const SetAreaFlagPayload* flag = (const SetAreaFlagPayload*)recordPayload;
            command.flagType = flag->flagType;
            command.flagValue = flag->value;
            command.flagIndex = flag->index;
        } else {
            return false;
        }

        if (!commandSemanticsValid(command))
            return false;
        outCommands[i] = command;
        offset += record->size;
    }

    if (offset != size)
        return false;
    *outCommandCount = section->commandCount;
    return true;
}

static uint32_t commandRecordSize(const StateCommand& command)
{
    switch (command.type) {
    case COMMAND_SPAWN_ACTOR:   return kSpawnRecordSize;
    case COMMAND_DELETE_ACTORS: return kDeleteRecordSize;
    case COMMAND_SET_AREA_FLAG: return kAreaFlagRecordSize;
    default:                    return 0;
    }
}

static uint32_t buildStateImage(const uint8_t* baseImage, const StateCommand* commands,
                                uint32_t commandCount, uint8_t* out)
{
    if (commandCount > kMaxCommands)
        return 0;

    uint32_t payloadSize = 0;
    for (uint32_t i = 0; i < commandCount; ++i) {
        if (!commandSemanticsValid(commands[i]))
            return 0;
        const uint32_t recordSize = commandRecordSize(commands[i]);
        if (!recordSize || payloadSize > kMaxCommandBytes - recordSize)
            return 0;
        payloadSize += recordSize;
    }

    memcpy(out, baseImage, kBaseFileSize);
    if (!commandCount)
        return kBaseFileSize;

    CommandSectionHeader* section = (CommandSectionHeader*)(out + kBaseFileSize);
    section->magic = kCommandMagic;
    section->version = kCommandVersion;
    section->headerSize = sizeof(CommandSectionHeader);
    section->totalSize = sizeof(CommandSectionHeader) + payloadSize;
    section->commandCount = commandCount;
    section->payloadCrc32 = 0;

    uint32_t offset = kBaseFileSize + sizeof(CommandSectionHeader);
    for (uint32_t i = 0; i < commandCount; ++i) {
        const StateCommand& command = commands[i];
        CommandRecordHeader* record = (CommandRecordHeader*)(out + offset);
        record->type = command.type;
        record->size = (uint16_t)commandRecordSize(command);
        record->frameDelay = (command.frameDelay & kCommandFrameDelayMask) |
                             (command.enabled ? 0u : kCommandDisabledFlag);
        uint8_t* payload = out + offset + sizeof(*record);

        if (command.type == COMMAND_SPAWN_ACTOR) {
            SpawnActorPayload spawn = {};
            spawn.procId = command.procId;
            spawn.subtype = command.subtype;
            spawn.params = command.params;
            spawn.position = command.position;
            spawn.angle = command.angle;
            spawn.amount = command.amount ? command.amount : 1;
            memcpy(payload, &spawn, sizeof(spawn));
        } else if (command.type == COMMAND_DELETE_ACTORS) {
            DeleteActorsPayload remove = {};
            remove.procId = command.procId;
            memcpy(payload, &remove, sizeof(remove));
        } else {
            SetAreaFlagPayload flag = {};
            flag.flagType = (u8)command.flagType;
            flag.value = command.flagValue;
            flag.index = command.flagIndex;
            memcpy(payload, &flag, sizeof(flag));
        }
        offset += record->size;
    }

    section->payloadCrc32 =
        crc32(out + kBaseFileSize + sizeof(CommandSectionHeader), payloadSize);
    return kBaseFileSize + sizeof(CommandSectionHeader) + payloadSize;
}

static bool validName(const char* name)
{
    if (!name || !name[0])
        return true;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\')
            return false;
    }
    return true;
}

static bool folderPath(char* out, size_t size, const char* folder)
{
    if (!validName(folder))
        return false;
    const int n = folder && folder[0] ? snprintf(out, size, "%s/%s", kStateDir, folder)
                                      : snprintf(out, size, "%s", kStateDir);
    return n > 0 && (size_t)n < size;
}

static bool statePath(char* out, size_t size, const char* folder, const char* name)
{
    char dir[128];
    if (!name || !name[0] || !validName(name) || !folderPath(dir, sizeof(dir), folder))
        return false;
    const int n = snprintf(out, size, "%s/%s.bin", dir, name);
    return n > 0 && (size_t)n < size;
}

static Storage::ReadResult readState(const char* folder, const char* name, uint32_t* size)
{
    char path[192];
    if (!statePath(path, sizeof(path), folder, name))
        return Storage::READ_ERROR;
    return Storage::ReadBinary(path, s_fileBuf, sizeof(s_fileBuf), size);
}

static bool writeState(const char* folder, const char* name, const uint8_t* image,
                       uint32_t size)
{
    char dir[128];
    char path[192];
    return folderPath(dir, sizeof(dir), folder) && statePath(path, sizeof(path), folder, name) &&
           Storage::EnsureDir(dir) && Storage::WriteBinary(path, image, size);
}

static const char* selectedFolder()
{
    if (s_selectedTab <= 0 || s_selectedTab > s_folderCount)
        return "";
    return s_folders[s_selectedTab - 1];
}

static void visitFolder(const Storage::DirEntry& entry, void*)
{
    if (!entry.isDir || !validName(entry.name) || strlen(entry.name) >= (size_t)kNameMax)
        return;
    int insert = 0;
    while (insert < s_folderCount && strcmp(s_folders[insert], entry.name) < 0)
        ++insert;
    if (insert >= kMaxFolders)
        return;
    const int count = s_folderCount < kMaxFolders ? s_folderCount + 1 : s_folderCount;
    for (int i = count - 1; i > insert; --i)
        memcpy(s_folders[i], s_folders[i - 1], sizeof(s_folders[i]));
    copyName(s_folders[insert], sizeof(s_folders[insert]), entry.name);
    s_folderCount = count;
}

static void refreshFolders()
{
    char selected[kNameMax];
    copyName(selected, sizeof(selected), selectedFolder());

    s_folderCount = 0;
    if (Storage::EnsureDir(kStateDir))
        Storage::ListDir(kStateDir, visitFolder, nullptr);
    s_selectedTab = 0;
    for (int i = 0; selected[0] && i < s_folderCount; ++i) {
        if (strcmp(selected, s_folders[i]) == 0) {
            s_selectedTab = i + 1;
            break;
        }
    }
    s_foldersLoaded = true;
    s_listLoaded = false;
}

static void visitFile(const Storage::DirEntry& entry, void*)
{
    const size_t length = strlen(entry.name);
    if (entry.isDir || s_fileCount >= kMaxFiles || length <= 4 ||
        strcmp(entry.name + length - 4, ".bin") != 0 || length - 4 >= (size_t)kNameMax)
        return;
    memcpy(s_files[s_fileCount], entry.name, length - 4);
    s_files[s_fileCount][length - 4] = '\0';
    ++s_fileCount;
}

static void refreshList()
{
    s_fileCount = 0;
    char dir[128];
    if (folderPath(dir, sizeof(dir), selectedFolder()))
        Storage::ListDir(dir, visitFile, nullptr);
    s_listLoaded = true;
}

static void sanitizeName(const char* in, char* out, int outSize)
{
    int j = 0;
    for (int i = 0; in && in[i] && j < outSize - 1; ++i) {
        const char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            out[j++] = c;
        else if (c == ' ')
            out[j++] = '_';
    }
    out[j] = '\0';
    if (j == 0) {
        OSCalendarTime ct;
        OSTicksToCalendarTime(OSGetTime(), &ct);
        snprintf(out, (size_t)outSize, "state_%04d%02d%02d_%02d%02d%02d", ct.tm_year,
                 ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min, ct.tm_sec);
    }
}

static void setLastLoaded(const char* folder, const char* name)
{
    if (strcmp(s_lastFolder, folder) == 0 && strcmp(s_lastName, name) == 0)
        return;
    copyName(s_lastFolder, sizeof(s_lastFolder), folder);
    copyName(s_lastName, sizeof(s_lastName), name);
    Config::MarkDirty();
}

static bool loadBusy()
{
    return s_loadReady || s_ovPhase != OV_IDLE;
}

static bool stageNameEquals(const char* stage, const char* expected)
{
    return stage && expected && strncmp(stage, expected, dStage_NAME_LEN) == 0;
}

// Some Lakebed rooms do not list the saved spawn in their player table.
static bool useNativeRestartEntry(const char* stage)
{
    return stageNameEquals(stage, "D_MN01");
}

// Lakebed water and zone state must not see the captured scratch mid-create.
static bool useSelectiveInfoRestore(const char* stage)
{
    return stageNameEquals(stage, "D_MN01");
}

static void requestSave(const char* folder, const char* name)
{
    daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer();
    if (!link) {
        fail("Link is not loaded.");
        return;
    }

    StateHeader* hdr = (StateHeader*)s_fileBuf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = kMagic;
    hdr->version = kVersion;
    const char* stage = dStage_getStageName();
    for (int i = 0; i < (int)sizeof(hdr->stage) - 1 && stage[i]; ++i)
        hdr->stage[i] = stage[i];
    hdr->room = dStage_getRoomNo();
    hdr->layer = dStage_getLayer();
    hdr->spawn = dStage_getSpawn();
    hdr->pos = link->current.pos;
    hdr->angle = link->shape_angle.y;
    daHorse_c* horse = dComIfGp_getHorse();
    if (horse && daAlink_isHorseRiding(link)) {
        hdr->horsePos = horse->current.pos;
        hdr->horseAngle = horse->shape_angle.y;
        hdr->horseRoom = horse->current.roomNo;
        hdr->horseRiding = 1;
    }
    if (const dCamera_xform_c* cam = dCam_getDesiredXform()) {
        hdr->camAt = cam->mAt;
        hdr->camEye = cam->mEye;
        hdr->camValid = 1;
    }

    // Live stage flags only reach mSave[] at save points.
    dComIfGs_putSave();
    uint8_t* info = s_fileBuf + sizeof(StateHeader);
    memcpy(info, dComIfGs_getSaveData(), kInfoSize);
    // The stored mode describes the last transition, not where Link stands now.
    ((dSv_info_c*)info)->mRestart.mLastMode = daAlink_captureRestartMode(link);

    char stem[kNameMax];
    sanitizeName(name, stem, sizeof(stem));
    char where[132];
    locationText(where, sizeof(where), folder, stem);
    s_listLoaded = false;
    if (!writeState(folder, stem, s_fileBuf, kBaseFileSize)) {
        Logger::LogWarn("[savestate] save failed: %s", where);
        fail("Save failed: %s", where);
        return;
    }
    Logger::Log("[savestate] saved %s: stage %.8s room %d spawn %d layer %d%s", where,
                hdr->stage, (int)hdr->room, (int)hdr->spawn, (int)hdr->layer,
                hdr->horseRiding ? " on Epona" : "");
    setStatus("Saved %s", where);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, kTitle, "Saved %s",
                                    where);
}

static void requestLoad(const char* folder, const char* name)
{
    if (!name || !name[0]) {
        fail("No state loaded yet.");
        return;
    }
    if (loadBusy()) {
        fail("A state is already loading.");
        return;
    }
    if (!s_hooksInstalled) {
        fail("The loader is not ready yet.");
        return;
    }

    char where[132];
    locationText(where, sizeof(where), folder, name);
    uint32_t size = 0;
    const Storage::ReadResult r = readState(folder, name, &size);
    if (r == Storage::READ_MISSING) {
        Logger::LogWarn("[savestate] load: %s is missing", where);
        fail("State file no longer exists.");
        return;
    }
    if (r != Storage::READ_OK ||
        !parseStateImage(s_fileBuf, size, s_loadCommands, &s_loadCommandCount)) {
        s_loadCommandCount = 0;
        Logger::LogWarn("[savestate] load: %s is not a valid state (%u bytes)", where,
                        (unsigned)size);
        fail("State load failed or used an unsupported format.");
        return;
    }

    memcpy(&s_loadHeader, s_fileBuf, sizeof(s_loadHeader));
    memcpy(s_pendingStorage, s_fileBuf + sizeof(StateHeader), kInfoSize);
    sanitizeImage(s_pendingStorage);
    s_pendingInfo = s_pendingStorage;
    copyName(s_readyFolder, sizeof(s_readyFolder), folder);
    copyName(s_readyName, sizeof(s_readyName), name);
    s_applyPosNext = s_overridePosition;
    s_loadReady = true;
    setStatus("Loading %s ...", where);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Info, kTitle, "Loading %s",
                                    where);
    Logger::Log("[savestate] load queued: %s (%u command(s))", where,
                (unsigned)s_loadCommandCount);
}

static void requestDelete(const char* folder, const char* name)
{
    char path[192];
    char where[132];
    locationText(where, sizeof(where), folder, name);
    s_listLoaded = false;
    if (!statePath(path, sizeof(path), folder, name) || !Storage::Remove(path)) {
        Logger::LogWarn("[savestate] delete failed: %s", where);
        setStatus("Could not delete %s", where);
        return;
    }
    Logger::Log("[savestate] deleted %s", where);
    setStatus("Deleted %s", where);
}

bool BeginGameSaveLoad(const char* sourceName, const void* image, uint32_t size)
{
    if (!image || size < dSv_SERIALIZED_SIZE || loadBusy() || !s_hooksInstalled)
        return false;

    // The return place drives a real warp; a garbage stage name would chase a missing archive.
    const dSv_player_return_place_c* destination =
        &((const dSv_info_c*)image)->mReturnPlace;
    bool valid = destination->mName[0] &&
                 memchr(destination->mName, '\0', sizeof(destination->mName)) &&
                 destination->mRoomNo >= 0 && destination->mRoomNo <= 63;
    for (const char* c = destination->mName; valid && *c; ++c) {
        if (!isprint((unsigned char)*c))
            valid = false;
    }
    if (!valid) {
        Logger::LogWarn("[savestate] %s rejected: invalid return place",
                        sourceName && sourceName[0] ? sourceName : "save image");
        return false;
    }

    memset(s_pendingStorage, 0, kInfoSize);
    memcpy(s_pendingStorage, image, dSv_SERIALIZED_SIZE);
    sanitizeImage(s_pendingStorage);
    s_pendingInfo = s_pendingStorage;

    memset(&s_loadHeader, 0, sizeof(s_loadHeader));
    memcpy(s_loadHeader.stage, destination->mName, sizeof(s_loadHeader.stage));
    s_loadHeader.stage[sizeof(s_loadHeader.stage) - 1] = '\0';
    s_loadHeader.spawn = destination->mPlayerStatus;
    s_loadHeader.room = destination->mRoomNo;
    s_loadHeader.layer = dStage_LAYER_DEFAULT;

    s_applyPosNext = false;
    s_readyFolder[0] = '\0';
    s_readyName[0] = '\0';
    s_pendingSerialized = true;
    s_normalSaveResume = true;
    s_loadCommandCount = 0;
    s_loadReady = true;
    Logger::Log("[savestate] save image load queued: %s stage %s room %d spawn %d",
                sourceName && sourceName[0] ? sourceName : "save image", s_loadHeader.stage,
                (int)s_loadHeader.room, (int)s_loadHeader.spawn);
    return true;
}

static bool needsImmediateRuntimePrep(fopAc_ac_c* linkBeforeLoad)
{
    return linkBeforeLoad == nullptr || !mDoCPd_isControllerSelected() ||
           fopAcM_searchByName(fpcNm_d_a_title) != nullptr;
}

static void clearRestartStartMode()
{
    if (!s_pendingInfo)
        return;
    // The low nibble is a transition result (void-out, sand recovery) that would replay.
    dSv_restart_c* restart = &((dSv_info_c*)s_pendingInfo)->mRestart;
    restart->mLastMode &= ~0xFu;
}

static void prepareHorseRestartMode()
{
    if (!s_pendingInfo || !s_loadHeader.horseRiding)
        return;
    u32* mode = &((dSv_info_c*)s_pendingInfo)->mRestart.mLastMode;
    *mode = (*mode & ~0xFu) | 1u;
}

static void preparePositionRestartState()
{
    if (!s_pendingInfo || s_normalSaveResume || !s_applyPosNext)
        return;

    dSv_restart_c* restart = &((dSv_info_c*)s_pendingInfo)->mRestart;
    const bool nativeEntry = useNativeRestartEntry(s_loadHeader.stage);
    static const u32 kRestartRoomMask = 0x0000003Fu;
    static const u32 kRestartStartEventMask = 0xFF000000u;
    restart->mRoomNo = s_loadHeader.room;
    restart->mStartPoint = nativeEntry ? (s16)-1 : restart->mStartPoint;
    restart->mRoomPos = s_loadHeader.pos;
    restart->mRoomAngleY = s_loadHeader.angle;
    // A point -1 restart copies this verbatim; event byte 0 would replay the room's opening demo.
    if (nativeEntry)
        restart->mRoomParam =
            (restart->mRoomParam & ~(kRestartStartEventMask | kRestartRoomMask)) |
            kRestartStartEventMask | ((u32)s_loadHeader.room & kRestartRoomMask);
}

struct ActorDeleteContext {
    s16         procId;
    fopAc_ac_c* actors[64];
    uint32_t    count;
    uint32_t    matched;
};

static fopAc_ac_c* collectActorForDelete(fopAc_ac_c* actor, void* context)
{
    ActorDeleteContext* collect = (ActorDeleteContext*)context;
    if (!actor || fopAcM_GetName(actor) != collect->procId || actor == dComIfGp_getPlayer())
        return nullptr;
    ++collect->matched;
    if (collect->count < sizeof(collect->actors) / sizeof(collect->actors[0]))
        collect->actors[collect->count++] = actor;
    return nullptr;
}

static uint32_t deleteActorsByProcId(s16 procId)
{
    if (!actorProcIdValid(procId) || procId == fpcNm_PLACEHOLDER_LINK)
        return 0;

    ActorDeleteContext collect = {};
    collect.procId = procId;
    fopAcIt_Judge(collectActorForDelete, &collect);

    uint32_t accepted = 0;
    for (uint32_t i = 0; i < collect.count; ++i) {
        if (fpcM_Delete(collect.actors[i]))
            ++accepted;
    }
    if (collect.matched > collect.count)
        Logger::LogWarn("[savestate] delete %s: %u matched, capped at %u",
                        actorProcName(procId), (unsigned)collect.matched,
                        (unsigned)collect.count);
    return accepted;
}

static bool setCurrentAreaFlag(const StateCommand& command)
{
    volatile dSv_memBit_c* area = dComIfGs_getTempBit();
    if (!area || command.type != COMMAND_SET_AREA_FLAG)
        return false;

    switch (command.flagType) {
    case AREA_FLAG_CHEST:
        dComIfGs_setBit(area->mTbox, command.flagIndex, command.flagValue != 0);
        return true;
    case AREA_FLAG_SWITCH: {
        const dSv_info_setSwitch_t setter =
            (dSv_info_setSwitch_t)(uintptr_t)(command.flagValue ? kOnSwitchAddr
                                                                : kOffSwitchAddr);
        setter(dComIfGs_getSaveData(), command.flagIndex, dStage_getRoomNo());
        return true;
    }
    case AREA_FLAG_ITEM:
        dComIfGs_setBit(area->mItem, command.flagIndex, command.flagValue != 0);
        return true;
    case AREA_FLAG_DUNGEON_ITEM: {
        const u8 mask = (u8)(1u << command.flagIndex);
        if (command.flagValue)
            area->mDungeonItem |= mask;
        else
            area->mDungeonItem &= (u8)~mask;
        return true;
    }
    case AREA_FLAG_KEY_COUNT:
        area->mKeyNum = (u8)command.flagIndex;
        return true;
    default:
        return false;
    }
}

static void executeCommand(const StateCommand& command)
{
    if (command.type == COMMAND_SPAWN_ACTOR) {
        daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer();
        const s8 room = link ? link->current.roomNo : dStage_getRoomNo();
        const uint32_t amount = command.amount ? command.amount : 1;
        uint32_t queued = 0;
        for (uint32_t i = 0; i < amount; ++i) {
            if (fopAcM_create(command.procId, command.params, command.subtype, room,
                              &command.position, &command.angle))
                ++queued;
        }
        Logger::Log("[savestate] frame %u: spawn %s x%u at (%.1f, %.1f, %.1f): %u queued",
                    (unsigned)s_execFrame, actorProcName(command.procId), (unsigned)amount,
                    command.position.x, command.position.y, command.position.z,
                    (unsigned)queued);
    } else if (command.type == COMMAND_DELETE_ACTORS) {
        const uint32_t count = deleteActorsByProcId(command.procId);
        Logger::Log("[savestate] frame %u: delete %s: %u accepted", (unsigned)s_execFrame,
                    actorProcName(command.procId), (unsigned)count);
    } else if (command.type == COMMAND_SET_AREA_FLAG) {
        const bool applied = setCurrentAreaFlag(command);
        Logger::Log("[savestate] frame %u: area flag %u #%u = %u: %s", (unsigned)s_execFrame,
                    (unsigned)command.flagType, (unsigned)command.flagIndex,
                    (unsigned)command.flagValue, applied ? "applied" : "failed");
    }
}

static void cancelCommandExecution(const char* reason)
{
    if (s_execCommandCount > s_execCommandIndex)
        Logger::LogWarn("[savestate] canceled %u pending command(s): %s",
                        (unsigned)(s_execCommandCount - s_execCommandIndex), reason);
    s_execCommandCount = 0;
    s_execCommandIndex = 0;
    s_execFrame = 0;
    s_execStage[0] = '\0';
}

static void startCommandExecution()
{
    if (s_ovCommandsStarted)
        return;
    s_ovCommandsStarted = true;
    cancelCommandExecution("new save state initialized");
    if (!s_loadCommandCount)
        return;

    memcpy(s_execCommands, s_loadCommands, s_loadCommandCount * sizeof(StateCommand));
    s_execCommandCount = s_loadCommandCount;
    s_execCommandIndex = 0;
    s_execFrame = 0;
    memcpy(s_execStage, s_ovStage, sizeof(s_execStage));

    for (uint32_t i = 0; i < s_execCommandCount; ++i) {
        if (s_execCommands[i].type == COMMAND_DELETE_ACTORS)
            s_execCommands[i].frameDelay += kDeleteBaseFrameDelay;
    }

    for (uint32_t i = 1; i < s_execCommandCount; ++i) {
        const StateCommand command = s_execCommands[i];
        uint32_t j = i;
        while (j > 0 && s_execCommands[j - 1].frameDelay > command.frameDelay) {
            s_execCommands[j] = s_execCommands[j - 1];
            --j;
        }
        s_execCommands[j] = command;
    }
    Logger::Log("[savestate] scheduled %u command(s)", (unsigned)s_execCommandCount);
}

static void tickCommandExecution()
{
    if (!s_execCommandCount)
        return;
    if (dStage_warpPending() ||
        strncmp(dStage_getStageName(), s_execStage, sizeof(s_execStage)) != 0) {
        cancelCommandExecution("target stage changed");
        return;
    }
    // Killing an event's actors while it runs leaves the event system walking freed processes.
    if (!dComIfGp_getPlayer() || dEvt_isEventRunning())
        return;

    while (s_execCommandIndex < s_execCommandCount &&
           s_execCommands[s_execCommandIndex].frameDelay <= s_execFrame) {
        if (s_execCommands[s_execCommandIndex].enabled)
            executeCommand(s_execCommands[s_execCommandIndex]);
        ++s_execCommandIndex;
    }
    if (s_execCommandIndex >= s_execCommandCount) {
        Logger::Log("[savestate] command list completed");
        cancelCommandExecution("completed");
        return;
    }
    if (s_execFrame != UINT32_MAX)
        ++s_execFrame;
}

static void applyLoadedPosition()
{
    if (s_ovHorseRiding) {
        daHorse_c* horse = dComIfGp_getHorse();
        if (horse)
            daHorse_setPosition(horse, &s_ovHorsePos, s_ovHorseAngle, s_ovHorseRoom);
    }
    daAlink_setPos((daAlink_c*)dComIfGp_getPlayer(), &s_ovPos, s_ovAngle, s_ovRoom);
    dStage_setRestartRoomPos(&s_ovPos, s_ovAngle, s_ovRoom);
}

static void endLoad()
{
    s_pendingInfo = nullptr;
    s_ovPhase = OV_IDLE;
    s_ovHorseRiding = false;
    s_ovHorseSpawnRequested = false;
    s_ovHorseWait = 0;
    s_ovReadyFrames = 0;
    s_ovCommandsStarted = false;
    s_ovEarlyStageFlagsRestored = false;
    s_ovEarlyZoneRoomMask[0] = 0;
    s_ovEarlyZoneRoomMask[1] = 0;
    s_loadCommandCount = 0;
    s_pendingSerialized = false;
    s_normalSaveResume = false;
}

static void notifyNamedLoad(Notifications::Kind kind, const char* message)
{
    if (!s_ovLabel[0])
        return;
    if (kind == Notifications::Success)
        Notifications::ShowKeyedTitledf(kNotifyKey, kind, kTitle, "Loaded %s", s_ovLabel);
    else
        Notifications::ShowKeyed(kNotifyKey, kind, kTitle, message);
    s_ovLabel[0] = '\0';
}

static void finishLoad()
{
    endLoad();
    setStatus("Loaded.");
    notifyNamedLoad(Notifications::Success, nullptr);
}

static void restoreTemporaryEventFlags(const uint8_t* snapshot)
{
    if (snapshot)
        memcpy(&dComIfGs_getSaveData()->mTmp, &((const dSv_info_c*)snapshot)->mTmp,
               sizeof(dSv_event_c));
}

// Zone slots are handed out as rooms stream in, so match records by room, not by slot.
static void restoreZoneFlags(const uint8_t* snapshot)
{
    if (!snapshot)
        return;
    const dSv_zone_c* savedZones = ((const dSv_info_c*)snapshot)->mZone;
    volatile dSv_zone_c* liveZones = (volatile dSv_zone_c*)dComIfGs_getSaveData()->mZone;
    for (int savedSlot = 0; savedSlot < dSv_ZONE_COUNT; ++savedSlot) {
        const dSv_zone_c& savedRecord = savedZones[savedSlot];
        if (savedRecord.mRoomNo < 0 || savedRecord.mRoomNo >= 64)
            continue;
        for (int liveSlot = 0; liveSlot < dSv_ZONE_COUNT; ++liveSlot) {
            volatile dSv_zone_c& liveRecord = liveZones[liveSlot];
            if (liveRecord.mRoomNo != savedRecord.mRoomNo)
                continue;
            memcpy((void*)liveRecord.mFlags, savedRecord.mFlags, sizeof(savedRecord.mFlags));
            break;
        }
    }
}

static bool restoreZoneFlagsForRoom(const uint8_t* snapshot, s8 roomNo, int liveSlot)
{
    if (!snapshot || roomNo < 0 || roomNo >= 64 || liveSlot < 0 || liveSlot >= dSv_ZONE_COUNT)
        return false;

    const dSv_zone_c* savedZones = ((const dSv_info_c*)snapshot)->mZone;
    const dSv_zone_c* savedRecord = nullptr;
    for (int slot = 0; slot < dSv_ZONE_COUNT; ++slot) {
        if (savedZones[slot].mRoomNo == roomNo) {
            savedRecord = &savedZones[slot];
            break;
        }
    }
    if (!savedRecord)
        return false;

    volatile dSv_zone_c& liveRecord =
        ((volatile dSv_zone_c*)dComIfGs_getSaveData()->mZone)[liveSlot];
    if (liveRecord.mRoomNo != roomNo)
        return false;
    memcpy((void*)liveRecord.mFlags, savedRecord->mFlags, sizeof(savedRecord->mFlags));
    return true;
}

static void restoreFullInfoSnapshot(uint8_t* snapshot, bool normalizeSerializedImage)
{
    if (!snapshot)
        return;
    if (normalizeSerializedImage) {
        dComIfGs_prepareLoadRuntime();
        dComIfGs_loadImage(snapshot);
        memcpy(snapshot, dComIfGs_getSaveData(), dSv_SERIALIZED_SIZE);
    } else {
        memcpy(dComIfGs_getSaveData(), snapshot, dSv_SERIALIZED_SIZE);
    }
    memcpy((u8*)dComIfGs_getSaveData() + dSv_SERIALIZED_SIZE, snapshot + dSv_SERIALIZED_SIZE,
           kInfoSize - dSv_SERIALIZED_SIZE);
}

static void restoreInfoSnapshot(uint8_t* snapshot, bool normalizeSerializedImage,
                                bool restoreLiveStageChunks)
{
    if (!snapshot)
        return;
    if (normalizeSerializedImage) {
        // Without the game's own pre-deserialize reset, Meter2 can start with a null runtime.
        dComIfGs_prepareLoadRuntime();
        dComIfGs_loadImage(snapshot);
        memcpy(snapshot, dComIfGs_getSaveData(), dSv_SERIALIZED_SIZE);
    } else {
        memcpy(dComIfGs_getSaveData(), snapshot, dSv_SERIALIZED_SIZE);
    }
    dSv_info_c* live = dComIfGs_getSaveData();
    const dSv_info_c* saved = (const dSv_info_c*)snapshot;
    if (restoreLiveStageChunks) {
        live->mMemory = saved->mMemory;
        memcpy(live->mDan, saved->mDan, sizeof(live->mDan));
    }
    live->mRestart = saved->mRestart;
}

static void onScenePhase1()
{
    if (s_ovPhase != OV_TEARDOWN)
        return;

    if (s_pendingInfo) {
        if (s_pendingSerialized) {
            dComIfGs_prepareLoadRuntime();
            dComIfGs_loadImage(s_pendingInfo);
            memcpy(s_pendingInfo, dComIfGs_getSaveData(), kInfoSize);
            s_pendingSerialized = false;
        } else if (useSelectiveInfoRestore(s_ovStage)) {
            restoreInfoSnapshot(s_pendingInfo, true, false);
            restoreTemporaryEventFlags(s_pendingInfo);
        } else {
            restoreFullInfoSnapshot(s_pendingInfo, true);
        }
    }

    s_ovPhase = OV_WAIT;
    s_ovWait = kWaitTimeout;
}

static bool getPendingRoomNumber(void* roomScene, s8* outRoomNo)
{
    if (!roomScene)
        return false;
    const s32 roomValue = *(volatile const s32*)((const u8*)roomScene + dScnRoom_OFF_ROOMNO);
    if (roomValue < 0 || roomValue >= 64)
        return false;
    *outRoomNo = (s8)roomValue;
    return true;
}

// Room actors order their events while room.dzr parses, so the saved stage flags go in first.
static void onRoomCreateBegin(void* roomScene)
{
    if (!s_pendingInfo || s_normalSaveResume || s_ovEarlyStageFlagsRestored ||
        (s_ovPhase != OV_WAIT && s_ovPhase != OV_APPLY))
        return;

    s8 roomNo = -1;
    if (!getPendingRoomNumber(roomScene, &roomNo) || roomNo != s_ovRoom)
        return;

    const s8 savedStageNo = dComIfGs_getStageNo((const dSv_info_c*)s_pendingInfo);
    if (savedStageNo < 0 || savedStageNo >= dSv_SAVE_COUNT)
        return;

    dSv_info_getSave(dComIfGs_getSaveData(), savedStageNo);
    memcpy(dComIfGs_getSaveData()->mDan, ((const dSv_info_c*)s_pendingInfo)->mDan,
           sizeof(dSv_info_c::mDan));
    restoreTemporaryEventFlags(s_pendingInfo);
    s_ovEarlyStageFlagsRestored = true;
}

static void onRoomZoneReady(void* roomScene)
{
    if (!s_pendingInfo || s_normalSaveResume || (s_ovPhase != OV_WAIT && s_ovPhase != OV_APPLY))
        return;

    s8 roomNo = -1;
    if (!getPendingRoomNumber(roomScene, &roomNo))
        return;
    const s8 zoneSlot = dStage_getRoomZoneNo(roomNo);
    if (zoneSlot < 0 || zoneSlot >= dSv_ZONE_COUNT)
        return;
    const uint32_t word = (uint32_t)roomNo >> 5;
    const uint32_t bit = 1u << ((uint32_t)roomNo & 31u);
    if ((s_ovEarlyZoneRoomMask[word] & bit) == 0 &&
        restoreZoneFlagsForRoom(s_pendingInfo, roomNo, zoneSlot))
        s_ovEarlyZoneRoomMask[word] |= bit;
}

static int scenePhase1Hook(void* scene)
{
    onScenePhase1();
    return ((dScnPly_phase1_t)(uintptr_t)TPHD_ADDR_dScnPly_phase1)(scene);
}

static int roomZonePhaseHook(void* roomScene)
{
    onRoomCreateBegin(roomScene);
    const int result =
        ((dScnRoom_zone_phase_t)(uintptr_t)TPHD_ADDR_dScnRoom_zone_phase)(roomScene);
    onRoomZoneReady(roomScene);
    return result;
}

static volatile uint32_t* phase1Slot()
{
    return (volatile uint32_t*)TPHD_DATA(TPHD_DATA_ADDR_dScnPly_phase1_slot);
}

static volatile uint32_t* zonePhaseSlot()
{
    return (volatile uint32_t*)TPHD_DATA(TPHD_DATA_ADDR_dScnRoom_zone_phase_slot);
}

static uint32_t phase1HookAddr()
{
    return (uint32_t)(uintptr_t)&scenePhase1Hook;
}

static uint32_t zoneHookAddr()
{
    return (uint32_t)(uintptr_t)&roomZonePhaseHook;
}

static bool claimSlot(volatile uint32_t* slot, uint32_t native, uint32_t hook)
{
    const uint32_t current = *slot;
    if (current == hook)
        return true;
    if (current != native)
        return false;
    *slot = hook;
    DCFlushRange((void*)slot, sizeof(*slot));
    return true;
}

static void releaseSlot(volatile uint32_t* slot, uint32_t native, uint32_t hook)
{
    if (*slot != hook)
        return;
    *slot = native;
    DCFlushRange((void*)slot, sizeof(*slot));
}

static void installHooks()
{
    if (s_hooksInstalled)
        return;
    s_phase1Claimed = claimSlot(phase1Slot(), TPHD_ADDR_dScnPly_phase1, phase1HookAddr());
    s_zoneClaimed = claimSlot(zonePhaseSlot(), TPHD_ADDR_dScnRoom_zone_phase, zoneHookAddr());
    OSMemoryBarrier();
    if (s_phase1Claimed && s_zoneClaimed) {
        s_hooksInstalled = true;
        Logger::Log("[savestate] scene phase hooks installed");
        return;
    }
    const uint32_t phase1 = *phase1Slot();
    const uint32_t zone = *zonePhaseSlot();
    if (!s_hookWarned && ((!s_phase1Claimed && phase1) || (!s_zoneClaimed && zone))) {
        s_hookWarned = true;
        Logger::LogWarn("[savestate] scene phase slots hold %08X / %08X, waiting for %08X / %08X",
                        (unsigned)phase1, (unsigned)zone, (unsigned)TPHD_ADDR_dScnPly_phase1,
                        (unsigned)TPHD_ADDR_dScnRoom_zone_phase);
    }
}

static void removeHooks()
{
    if (s_phase1Claimed)
        releaseSlot(phase1Slot(), TPHD_ADDR_dScnPly_phase1, phase1HookAddr());
    if (s_zoneClaimed)
        releaseSlot(zonePhaseSlot(), TPHD_ADDR_dScnRoom_zone_phase, zoneHookAddr());
    OSMemoryBarrier();
    s_phase1Claimed = false;
    s_zoneClaimed = false;
    s_hooksInstalled = false;
}

static void consumeLoadRequest()
{
    cancelCommandExecution("another save state load began");
    s_loadReady = false;
    s_ovLabel[0] = '\0';
    if (s_readyName[0]) {
        clearRestartStartMode();
        setLastLoaded(s_readyFolder, s_readyName);
        locationText(s_ovLabel, sizeof(s_ovLabel), s_readyFolder, s_readyName);
        s_readyFolder[0] = '\0';
        s_readyName[0] = '\0';
    }
    if (needsImmediateRuntimePrep(dComIfGp_getPlayer()))
        Controller::EnsureSelected();
    preparePositionRestartState();
    prepareHorseRestartMode();

    memcpy(s_ovStage, s_loadHeader.stage, sizeof(s_ovStage));
    s_ovPos = s_loadHeader.pos;
    s_ovAngle = s_loadHeader.angle;
    s_ovRoom = s_loadHeader.room;
    s_ovCamAt = s_loadHeader.camAt;
    s_ovCamEye = s_loadHeader.camEye;
    s_ovCamValid = s_applyPosNext && s_loadHeader.camValid != 0;
    s_ovHorsePos = s_loadHeader.horsePos;
    s_ovHorseAngle = s_loadHeader.horseAngle;
    s_ovHorseRoom = s_loadHeader.horseRoom;
    s_ovHorseRiding = s_loadHeader.horseRiding != 0;
    s_ovHorseSpawnRequested = false;
    s_ovHorseWait = 0;
    s_ovReadyFrames = 0;
    s_ovApplyPos = s_applyPosNext;
    s_ovCommandsStarted = false;
    s_ovEarlyStageFlagsRestored = false;
    s_ovEarlyZoneRoomMask[0] = 0;
    s_ovEarlyZoneRoomMask[1] = 0;
    // Armed before the warp is visible to the engine.
    s_ovPhase = OV_TEARDOWN;
    s_ovWait = kPhase1Timeout;
    OSMemoryBarrier();

    if (s_normalSaveResume) {
        dStage_loadSavedGame(s_loadHeader.stage, s_loadHeader.room, s_loadHeader.spawn);
    } else {
        const bool nativeEntry = s_applyPosNext && useNativeRestartEntry(s_loadHeader.stage);
        const bool lakebedMain = stageNameEquals(s_loadHeader.stage, "D_MN01");
        dStage_loadStateDirect(s_loadHeader.stage, s_loadHeader.room,
                               nativeEntry ? (s16)-1 : s_loadHeader.spawn,
                               lakebedMain ? (s8)dStage_LAYER_DEFAULT : s_loadHeader.layer);
    }
    // Link's recreate reads the live mode, which can run before phase_1 installs ours.
    if (s_loadHeader.horseRiding)
        dStage_setRestartHorseStart();
}

static void tickWait()
{
    if (--s_ovWait <= 0) {
        Logger::LogWarn("[savestate] load timed out waiting for Link in %.8s", s_ovStage);
        endLoad();
        setStatus("State load timed out.");
        notifyNamedLoad(Notifications::Error, s_status);
        return;
    }

    daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer();
    const bool stageOk = strncmp(dStage_getStageName(), s_ovStage, sizeof(s_ovStage)) == 0;
    const bool targetReady = link && stageOk && !dStage_warpPending();
    // The actor heap often reuses Link's address, so readiness is counted, not compared.
    if (targetReady) {
        if (s_ovReadyFrames < kReadyConfirm)
            ++s_ovReadyFrames;
    } else {
        s_ovReadyFrames = 0;
    }
    if (s_ovReadyFrames < kReadyConfirm)
        return;

    if (s_ovHorseRiding) {
        daHorse_c* horse = dComIfGp_getHorse();
        if (!horse) {
            if (!s_ovHorseSpawnRequested && ++s_ovHorseWait >= kHorseSpawnGrace) {
                const cXyz& pos = s_ovApplyPos ? s_ovHorsePos : link->current.pos;
                const s16 angleY = s_ovApplyPos ? s_ovHorseAngle : link->shape_angle.y;
                const s8 room = s_ovApplyPos ? s_ovHorseRoom : link->current.roomNo;
                csXyz angle;
                angle.set(0, angleY, 0);
                if (fopAcM_create(fpcNm_d_a_horse, 0, -1, room, &pos, &angle)) {
                    s_ovHorseSpawnRequested = true;
                    Logger::Log("[savestate] spawned Epona in the target stage");
                } else {
                    s_ovHorseWait = kHorseSpawnGrace / 2;
                }
            }
            return;
        }
        if (s_ovHorseWait > 0 && !s_ovHorseSpawnRequested)
            s_ovHorseWait = 0;
        if (!daAlink_isHorseRiding(link))
            return;
    }

    if (s_pendingInfo && !s_normalSaveResume) {
        if (useSelectiveInfoRestore(s_ovStage)) {
            restoreInfoSnapshot(s_pendingInfo, false, true);
            restoreTemporaryEventFlags(s_pendingInfo);
            restoreZoneFlags(s_pendingInfo);
        } else {
            restoreFullInfoSnapshot(s_pendingInfo, false);
        }
    }
    if (s_normalSaveResume) {
        // Skipping the game's controller-select screen can leave the pad manager unselected.
        Controller::EnsureSelected();
        s_ctrlReselect = kControllerReselect;
    }
    if (!s_ovApplyPos) {
        startCommandExecution();
        finishLoad();
        return;
    }
    applyLoadedPosition();
    startCommandExecution();
    s_ovPhase = OV_APPLY;
    s_ovApply = s_ovHorseRiding ? kHorseHoldFrames : kHoldTimeout;
    s_ovGround = kGroundConfirm;
}

static void tickApply()
{
    daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer();
    if (!link || dStage_warpPending()) {
        finishLoad();
        return;
    }
    if (s_ovApplyPos) {
        applyLoadedPosition();
        *dComIfGp_getOxygen() = kFullOxygen;
        if (s_ovCamValid) {
            if (dCamera_xform_c* cam = dCam_getDesiredXform()) {
                cam->mAt = s_ovCamAt;
                cam->mEye = s_ovCamEye;
            }
        }
    }

    bool positionDone = false;
    if (!s_ovApplyPos) {
        positionDone = true;
    } else if (s_ovHorseRiding) {
        positionDone = --s_ovApply <= 0;
    } else {
        if (daAlink_onGround(link)) {
            if (--s_ovGround <= 0)
                positionDone = true;
        } else {
            s_ovGround = kGroundConfirm;
        }
        if (--s_ovApply <= 0)
            positionDone = true;
    }
    if (positionDone)
        finishLoad();
}

bool WantsInputBlock(uint32_t held)
{
    return s_reloadHotkey && Hotkeys::Matches(Hotkeys::HOTKEY_RELOAD_STATE, held);
}

void Tick(bool acceptInput)
{
    installHooks();

    if (s_ctrlReselect > 0) {
        --s_ctrlReselect;
        if (!mDoCPd_isControllerSelected())
            Controller::EnsureSelected();
    }

    if (acceptInput && s_reloadHotkey && Hotkeys::Pressed(Hotkeys::HOTKEY_RELOAD_STATE)) {
        Input::DrainHeld();
        requestLoad(s_lastFolder, s_lastName);
    }

    if (s_loadReady)
        consumeLoadRequest();

    switch (s_ovPhase) {
    case OV_TEARDOWN:
        if (--s_ovWait <= 0) {
            if (dStage_warpPending())
                dComIfGp_getNextStage()->mEnabled = 0;
            Logger::LogError("[savestate] load aborted: the scene phase hook did not run");
            endLoad();
            setStatus("State load aborted: the stage never reloaded.");
            notifyNamedLoad(Notifications::Error, s_status);
        }
        break;
    case OV_WAIT:
        tickWait();
        break;
    case OV_APPLY:
        tickApply();
        break;
    default:
        break;
    }

    tickCommandExecution();
}

void OnApplicationStart()
{
    s_pendingInfo = nullptr;
    s_loadReady = false;
    s_readyFolder[0] = '\0';
    s_readyName[0] = '\0';
    s_loadCommandCount = 0;
    s_ovLabel[0] = '\0';
    endLoad();
    s_execCommandCount = 0;
    s_execCommandIndex = 0;
    s_execFrame = 0;
    s_execStage[0] = '\0';
    s_ctrlReselect = 0;
    s_phase1Claimed = false;
    s_zoneClaimed = false;
    s_hooksInstalled = false;
    s_hookWarned = false;
    s_foldersLoaded = false;
    s_listLoaded = false;
    s_confirmDelete = -1;
    s_openDeletePopup = false;
    s_editorOpen = false;
    s_editorLoaded = false;
    s_editorCommandCount = 0;
    s_status[0] = '\0';
}

void OnApplicationEnd()
{
    removeHooks();
    s_loadReady = false;
    endLoad();
    cancelCommandExecution("application ended");
}

void ResetToDefaults()
{
    s_overridePosition = true;
    s_reloadHotkey = false;
    s_lastFolder[0] = '\0';
    s_lastName[0] = '\0';
}

void LoadConfig(const cJSON* root)
{
    Config::ReadBool(root, "saveStateOverridePosition", &s_overridePosition);
    Config::ReadBool(root, "saveStateReloadLastHotkey", &s_reloadHotkey);
    Config::ReadString(root, "saveStateLastLoadedFolder", s_lastFolder, sizeof(s_lastFolder));
    Config::ReadString(root, "saveStateLastLoaded", s_lastName, sizeof(s_lastName));
    if (!validName(s_lastFolder) || !validName(s_lastName)) {
        s_lastFolder[0] = '\0';
        s_lastName[0] = '\0';
    }
}

void SaveConfig(cJSON* root)
{
    Config::AddBool(root, "saveStateOverridePosition", s_overridePosition);
    Config::AddBool(root, "saveStateReloadLastHotkey", s_reloadHotkey);
    Config::AddString(root, "saveStateLastLoadedFolder", s_lastFolder);
    Config::AddString(root, "saveStateLastLoaded", s_lastName);
}

static const char* commandTypeName(uint16_t type)
{
    switch (type) {
    case COMMAND_SPAWN_ACTOR:   return "Spawn actor";
    case COMMAND_DELETE_ACTORS: return "Delete actors";
    case COMMAND_SET_AREA_FLAG: return "Set area flag";
    default:                    return "Unknown";
    }
}

static const char* areaFlagTypeName(uint16_t type)
{
    switch (type) {
    case AREA_FLAG_CHEST:        return "Chest";
    case AREA_FLAG_SWITCH:       return "Switch";
    case AREA_FLAG_ITEM:         return "Item";
    case AREA_FLAG_DUNGEON_ITEM: return "Dungeon item";
    case AREA_FLAG_KEY_COUNT:    return "Key count";
    default:                     return "Unknown";
    }
}

static void commandSummary(const StateCommand& command, char* out, size_t outSize)
{
    if (command.type == COMMAND_SPAWN_ACTOR) {
        char count[16] = "";
        if (command.amount > 1)
            snprintf(count, sizeof(count), "x%u ", (unsigned)command.amount);
        snprintf(out, outSize, "%s%s at %.1f, %.1f, %.1f", count,
                 actorDisplayName(command.procId), command.position.x, command.position.y,
                 command.position.z);
    } else if (command.type == COMMAND_DELETE_ACTORS) {
        snprintf(out, outSize, "All %s", actorDisplayName(command.procId));
    } else if (command.flagType == AREA_FLAG_KEY_COUNT) {
        snprintf(out, outSize, "Key count = %u", (unsigned)command.flagIndex);
    } else {
        snprintf(out, outSize, "%s %u = %s", areaFlagTypeName(command.flagType),
                 (unsigned)command.flagIndex, command.flagValue ? "On" : "Off");
    }
}

static void resetEditorForm(uint16_t type)
{
    memset(&s_editorForm, 0, sizeof(s_editorForm));
    s_editorForm.type = type;
    s_editorForm.procId = -1;
    s_editorForm.subtype = -1;
    s_editorForm.flagType = AREA_FLAG_SWITCH;
    s_editorForm.flagValue = 1;
    s_editorForm.amount = 1;
    s_editorForm.enabled = 1;
    if (daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer()) {
        s_editorForm.position = link->current.pos;
        s_editorForm.angle.y = link->shape_angle.y;
    }
    s_editorQuery[0] = '\0';
    s_editorFormIndex = -1;
}

static void openEditor(const char* folder, const char* name)
{
    copyName(s_editorFolder, sizeof(s_editorFolder), folder);
    copyName(s_editorName, sizeof(s_editorName), name);
    s_editorOpen = true;
    s_editorLoaded = false;
    s_editorCommandCount = 0;
    Ui::Window::RequestFocus(kEditorWindowName);

    uint32_t size = 0;
    const Storage::ReadResult r = readState(folder, name, &size);
    if (r != Storage::READ_OK ||
        !parseStateImage(s_fileBuf, size, s_editorCommands, &s_editorCommandCount)) {
        s_editorCommandCount = 0;
        setEditorStatus("%s", r == Storage::READ_MISSING ? "State file no longer exists."
                                                         : "This state file cannot be edited.");
        return;
    }
    memcpy(s_editorBase, s_fileBuf, kBaseFileSize);
    s_editorLoaded = true;
    setEditorStatus("%u command(s)", (unsigned)s_editorCommandCount);
    resetEditorForm(COMMAND_SPAWN_ACTOR);
}

static bool persistEditorCommands(const char* okStatus)
{
    if (!s_editorLoaded) {
        setEditorStatus("The state has not finished loading.");
        return false;
    }
    const uint32_t size =
        buildStateImage(s_editorBase, s_editorCommands, s_editorCommandCount, s_fileBuf);
    if (!size) {
        setEditorStatus("Unable to save the command list.");
        return false;
    }
    char where[132];
    locationText(where, sizeof(where), s_editorFolder, s_editorName);
    s_listLoaded = false;
    if (!writeState(s_editorFolder, s_editorName, s_fileBuf, size)) {
        Logger::LogWarn("[savestate] command save failed: %s", where);
        setEditorStatus("Could not write the state file.");
        Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Error, kTitle,
                                        "Save failed: %s", where);
        return false;
    }
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, kTitle, "Saved %s",
                                    where);
    if (okStatus)
        setEditorStatus("%s", okStatus);
    return true;
}

static void closeEditor()
{
    s_editorOpen = false;
    s_editorLoaded = false;
    s_editorCommandCount = 0;
}

static void saveEditorCommands()
{
    if (!persistEditorCommands(nullptr))
        return;
    setStatus("Saved commands for %s", s_editorName);
    closeEditor();
}

static void drawActorSelector()
{
    ImGui::SetNextItemWidth(240.0f);
    Ui::Field::TextWithHint("##actorfilter", "Search actors", s_editorQuery,
                            sizeof(s_editorQuery));
    if (s_editorQuery[0]) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            s_editorQuery[0] = '\0';
    }

    const s16 selected = s_editorForm.procId;
    const char* preview = actorProcIdValid(selected) ? actorDisplayName(selected) : "Select...";
    if (!ImGui::BeginCombo("Actor", preview))
        return;
    const uint32_t count = (uint32_t)(procs_bin_size / kProcRecordSize);
    for (uint32_t i = 0; i < count; ++i) {
        const s16 id = (s16)i;
        if (!actorProcIdValid(id))
            continue;
        const char* name = actorProcName(id);
        const char* friendly = actorFriendlyName(id);
        if (!containsIgnoreCase(name, s_editorQuery) &&
            !containsIgnoreCase(friendly, s_editorQuery))
            continue;
        char label[96];
        if (friendly[0])
            snprintf(label, sizeof(label), "%s  (%s)##%u", friendly, name, (unsigned)i);
        else
            snprintf(label, sizeof(label), "%s##%u", name, (unsigned)i);
        const bool isSelected = selected == id;
        if (ImGui::Selectable(label, isSelected))
            s_editorForm.procId = id;
        if (isSelected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

static int clampInt(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static void drawSpawnFields()
{
    drawActorSelector();
    Ui::Field::U32("Parameters", &s_editorForm.params, "%08X");
    int subtype = s_editorForm.subtype;
    if (Ui::Field::Int("Subtype", &subtype))
        s_editorForm.subtype = (s8)clampInt(subtype, -128, 127);
    int amount = s_editorForm.amount ? s_editorForm.amount : 1;
    if (Ui::Field::Int("Amount", &amount))
        s_editorForm.amount = (u16)clampInt(amount, 1, kMaxSpawnAmount);
    Ui::Field::Float("X", &s_editorForm.position.x);
    Ui::Field::Float("Y", &s_editorForm.position.y);
    Ui::Field::Float("Z", &s_editorForm.position.z);
    int facing = s_editorForm.angle.y;
    if (Ui::Field::Int("Facing", &facing))
        s_editorForm.angle.y = (s16)clampInt(facing, -32768, 32767);
    if (ImGui::Button("Use Link's position")) {
        if (daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer()) {
            s_editorForm.position = link->current.pos;
            s_editorForm.angle.y = link->shape_angle.y;
        }
    }
}

static void drawAreaFlagFields()
{
    static const char* kFlagTypes[] = { "Chest", "Switch", "Item", "Dungeon item", "Key count" };
    int flagType = s_editorForm.flagType;
    if (flagType < 0 || flagType > AREA_FLAG_KEY_COUNT)
        flagType = AREA_FLAG_SWITCH;
    if (ImGui::Combo("Flag type", &flagType, kFlagTypes, 5))
        s_editorForm.flagType = (uint16_t)flagType;

    int value = s_editorForm.flagIndex;
    if (s_editorForm.flagType == AREA_FLAG_KEY_COUNT) {
        if (Ui::Field::Int("Keys", &value))
            s_editorForm.flagIndex = (u16)clampInt(value, 0, 255);
        return;
    }
    const int maximum = s_editorForm.flagType == AREA_FLAG_CHEST    ? 63
                        : s_editorForm.flagType == AREA_FLAG_SWITCH ? 127
                        : s_editorForm.flagType == AREA_FLAG_ITEM   ? 31
                                                                    : 7;
    if (Ui::Field::Int("Flag", &value))
        s_editorForm.flagIndex = (u16)clampInt(value, 0, maximum);
    bool on = s_editorForm.flagValue != 0;
    if (ImGui::Checkbox("Set", &on))
        s_editorForm.flagValue = on ? 1 : 0;
}

static void drawCommandForm()
{
    static const char* kTypes[] = { "Spawn actor", "Delete actors", "Set area flag" };
    int typeIndex = (int)s_editorForm.type - 1;
    if (typeIndex < 0 || typeIndex >= 3)
        typeIndex = 0;
    if (ImGui::Combo("Action", &typeIndex, kTypes, 3)) {
        const uint32_t delay = s_editorForm.frameDelay;
        const s16 keepProc = s_editorForm.procId;
        const u16 keepAmount = s_editorForm.amount;
        const uint16_t newType = (uint16_t)(typeIndex + 1);
        resetEditorForm(newType);
        s_editorForm.frameDelay = delay;
        if (newType == COMMAND_SPAWN_ACTOR || newType == COMMAND_DELETE_ACTORS) {
            s_editorForm.procId = keepProc;
            s_editorForm.amount = keepAmount;
        }
    }

    int delay = (int)(s_editorForm.frameDelay & kCommandFrameDelayMask);
    if (Ui::Field::Int("Delay (frames)", &delay, 1, 30))
        s_editorForm.frameDelay = (uint32_t)(delay < 0 ? 0 : delay);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frames to wait after the state finishes loading.");

    if (s_editorForm.type == COMMAND_SPAWN_ACTOR) {
        drawSpawnFields();
    } else if (s_editorForm.type == COMMAND_DELETE_ACTORS) {
        drawActorSelector();
        if (s_editorForm.procId == fpcNm_PLACEHOLDER_LINK)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Link cannot be deleted.");
    } else {
        drawAreaFlagFields();
    }

    const bool valid = commandSemanticsValid(s_editorForm);
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(s_editorFormIndex >= 0 ? "Update command" : "Add command")) {
        if (s_editorFormIndex >= 0 && (uint32_t)s_editorFormIndex < s_editorCommandCount) {
            s_editorCommands[s_editorFormIndex] = s_editorForm;
            setEditorStatus("Command updated.");
        } else if (s_editorCommandCount < kMaxCommands) {
            s_editorCommands[s_editorCommandCount++] = s_editorForm;
            setEditorStatus("Command added.");
        } else {
            setEditorStatus("The command list is full.");
        }
        resetEditorForm(s_editorForm.type);
    }
    ImGui::EndDisabled();
    if (s_editorFormIndex >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel edit"))
            resetEditorForm(COMMAND_SPAWN_ACTOR);
    }
}

static void removeEditorCommand(int index)
{
    for (uint32_t j = (uint32_t)index + 1; j < s_editorCommandCount; ++j)
        s_editorCommands[j - 1] = s_editorCommands[j];
    --s_editorCommandCount;
    if (s_editorFormIndex == index)
        resetEditorForm(COMMAND_SPAWN_ACTOR);
    else if (s_editorFormIndex > index)
        --s_editorFormIndex;
}

static void drawCommandTable()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##state_commands", 5, flags, ImVec2(0.0f, 230.0f)))
        return;
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Delay", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < (int)s_editorCommandCount; ++i) {
        StateCommand& command = s_editorCommands[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool on = command.enabled != 0;
        // Toggling saves at once, so skipping a command needs no separate save.
        if (ImGui::Checkbox("##enabled", &on)) {
            command.enabled = on ? 1 : 0;
            persistEditorCommands(on ? "Command enabled." : "Command disabled.");
        }
        ImGui::BeginDisabled(!command.enabled);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u", (unsigned)command.frameDelay);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(commandTypeName(command.type));
        ImGui::TableSetColumnIndex(3);
        char summary[192];
        commandSummary(command, summary, sizeof(summary));
        ImGui::TextUnformatted(summary);
        ImGui::EndDisabled();
        ImGui::TableSetColumnIndex(4);
        if (ImGui::SmallButton("Edit")) {
            s_editorForm = command;
            s_editorFormIndex = i;
            s_editorQuery[0] = '\0';
        }
        ImGui::SameLine();
        const bool remove = ImGui::SmallButton("Delete");
        ImGui::PopID();
        if (remove) {
            removeEditorCommand(i);
            --i;
        }
    }
    ImGui::EndTable();
}

static void drawCommandEditor()
{
    if (!s_editorOpen)
        return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kEditorWindowName, &s_editorOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("State: %s/%s", s_editorFolder[0] ? s_editorFolder : "Main", s_editorName);
        if (s_editorStatus[0])
            ImGui::TextDisabled("%s", s_editorStatus);
        ImGui::Separator();

        if (!s_editorLoaded) {
            if (ImGui::Button("Close"))
                closeEditor();
        } else {
            drawCommandForm();
            ImGui::Separator();
            drawCommandTable();
            if (ImGui::Button("Save changes"))
                saveEditorCommands();
            ImGui::SameLine();
            if (ImGui::Button("Close"))
                closeEditor();
        }
    }
    ImGui::End();
    if (!s_editorOpen)
        closeEditor();
}

static void drawFolderTabs()
{
    if (!ImGui::BeginTabBar("##state_folders"))
        return;
    if (ImGui::BeginTabItem("Main")) {
        if (s_selectedTab != 0) {
            s_selectedTab = 0;
            s_listLoaded = false;
        }
        ImGui::EndTabItem();
    }
    for (int i = 0; i < s_folderCount; ++i) {
        char label[kNameMax];
        copyName(label, sizeof(label), s_folders[i]);
        for (char* p = label; *p; ++p) {
            if (*p == '#')
                *p = '_';
        }
        if (label[0] >= 'a' && label[0] <= 'z')
            label[0] = (char)(label[0] - 'a' + 'A');
        char tabId[160];
        snprintf(tabId, sizeof(tabId), "%.63s##%.63s", label, s_folders[i]);
        if (ImGui::BeginTabItem(tabId)) {
            if (s_selectedTab != i + 1) {
                s_selectedTab = i + 1;
                s_listLoaded = false;
            }
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

static void drawDeletePopup()
{
    if (s_openDeletePopup) {
        ImGui::OpenPopup("Delete state?");
        s_openDeletePopup = false;
    }
    if (s_confirmDelete < 0 || s_confirmDelete >= s_fileCount ||
        !ImGui::BeginPopupModal("Delete state?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::Text("Delete \"%s\"?", s_files[s_confirmDelete]);
    ImGui::Spacing();
    if (ImGui::Button("Delete")) {
        requestDelete(selectedFolder(), s_files[s_confirmDelete]);
        s_confirmDelete = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        s_confirmDelete = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawMenuItem()
{
    Ui::Window::Checkbox("Save Loader", &s_open, kWindowName);
}

void DrawWindow()
{
    if (!s_open)
        return;
    if (!s_foldersLoaded)
        refreshFolders();

    ImGui::SetNextWindowPos(ImVec2(370.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (ImGui::Checkbox("Restore exact position", &s_overridePosition))
            Config::MarkDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: start from the saved spawn point instead.");
        if (ImGui::Checkbox("Reload last state hotkey", &s_reloadHotkey))
            Config::MarkDirty();
        ImGui::SameLine(0.0f, 0.0f);
        Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_RELOAD_STATE), " (", ")", true);
        if (s_lastName[0])
            ImGui::TextDisabled("Last loaded: %s/%s", s_lastFolder[0] ? s_lastFolder : "Main",
                                s_lastName);

        drawFolderTabs();
        if (!s_listLoaded)
            refreshList();

        ImGui::SetNextItemWidth(400.0f);
        Ui::Field::TextWithHint("##name", "state name (optional)", s_nameBuf, sizeof(s_nameBuf));
        ImGui::SameLine();
        if (ImGui::Button("Save State")) {
            requestSave(selectedFolder(), s_nameBuf);
            s_nameBuf[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            refreshFolders();
            refreshList();
        }
        if (s_status[0])
            ImGui::TextDisabled("%s", s_status);

        ImGui::Separator();
        ImGui::BeginChild("##states", ImVec2(0.0f, 0.0f), true);
        if (s_fileCount == 0)
            ImGui::TextDisabled("No saved states yet.");
        for (int i = 0; i < s_fileCount; ++i) {
            ImGui::PushID(i);
            if (ImGui::Button("Load"))
                requestLoad(selectedFolder(), s_files[i]);
            ImGui::SameLine();
            if (ImGui::Button("Edit"))
                openEditor(selectedFolder(), s_files[i]);
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                s_confirmDelete = i;
                s_openDeletePopup = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(s_files[i]);
            ImGui::PopID();
        }
        ImGui::EndChild();
        drawDeletePopup();
    }
    ImGui::End();
    drawCommandEditor();
}
}
}
