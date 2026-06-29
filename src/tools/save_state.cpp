// tools/save_state.cpp -- see save_state.h.
//
// A save-state snapshots the whole persistent save block (dSv_info_c, 0x13D8
// bytes at g_dComIfG_gameInfo.info = 0x10145348) plus the current
// stage/room/layer/spawn and Link's exact position/facing through the
// build-specific Storage backend. The info block is the live store for hearts,
// rupees, items, events and area flags, so snapshotting it captures full game
// state.
// Loading restores the block, does a full save-load warp to the stage (which
// rebuilds the runtime from the block), re-stamps the block once the reload has
// settled (the transition can clobber it mid-flight -- mirrors dusklight's
// tickPendingApply), then forces Link to the saved position and refills air.
// File I/O runs on a background thread; the restore/warp is applied on the
// present thread via Tick().
#include "tools/save_state.h"

#include "imgui.h"
#include "overlay.h"            // ov::g_settings.controllerPref
#include "input.h"
#include "game/game.h"
#include "storage.h"
#include "logger.h"
#include "procs_bin.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <malloc.h>     // memalign -- FS read/write buffers need 0x40 alignment

#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/messagequeue.h>
#include <coreinit/time.h>

namespace Tools {
namespace SaveState {

// ---- on-disk format ---------------------------------------------------------
static const uint32_t kMagic   = 0x54505353;  // 'TPSS'
static const uint32_t kVersion = 8;           // v8 = extensible post-load commands

struct StateHeader {
    uint32_t magic;
    uint32_t version;
    char     stage[8];
    s8       room;
    s8       layer;
    s16      spawn;     // start point
    cXyz     pos;       // Link's exact position
    s16      angle;     // shape_angle.y
    // Camera transform (the decoupled at/eye the camera renders from), restored
    // so a load returns to the exact view, not a default reframe (tpgz's REQ_CAM).
    cXyz     camAt;
    cXyz     camEye;
    // Epona has her own actor transform. Link's mounted position is close, but
    // not interchangeable with the horse base on slopes and during animation.
    cXyz     horsePos;
    s16      horseAngle;
    s8       horseRoom;
    u8       horseRiding; // 1 when Link was mounted on Epona at capture time
    u8       camValid;  // 1 = camera was captured
    u8       _pad[3];
    // Hearts/rupees/items/flags live in the fixed save block that follows.
};  // immediately followed by GAME_DSVINFO_SIZE raw save bytes

static_assert(sizeof(StateHeader) == 80, "StateHeader v8 layout changed");

static const uint32_t kInfoSize  = GAME_DSVINFO_SIZE;
static const uint32_t kBaseFileSize = sizeof(StateHeader) + GAME_DSVINFO_SIZE;

static const uint32_t kCommandMagic   = 0x54504344; // 'TPCD'
static const uint16_t kCommandVersion = 1;
static const uint32_t kMaxCommands    = 128;
static const uint32_t kMaxCommandBytes = 8192;

// 'enabled' rides in the high bit of the on-disk frameDelay: existing files never
// set it (and only ever store small delays), so they load as enabled -- no format
// version bump and no data loss. The low 31 bits remain the frame delay.
static const uint32_t kCommandDisabledFlag   = 0x80000000u;
static const uint32_t kCommandFrameDelayMask = 0x7FFFFFFFu;
// Spawn 'amount' reuses SpawnActorPayload's former reserved1 slot (0 -> 1 copy),
// so old files stay valid. Capped so one command can't drain the actor pool.
static const uint16_t kMaxSpawnAmount = 64;
// Deleting actors the instant a scene becomes ready can hit stage actors still in
// their async creation phase and crash. Delete commands therefore always run this
// many frames after the load settles; the user's frame delay adds on top.
static const uint32_t kDeleteBaseFrameDelay = 10;

enum CommandType {
    COMMAND_SPAWN_ACTOR = 1,
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
    u16   amount;       // was reserved1; copies to spawn (0 == 1 for old files)
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
    u16      amount;     // spawn copies (>= 1); unused by other types
    u8       enabled;    // 0 = skipped during the load sequence
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

// ---- background worker (save write / load read / delete) --------------------
enum { OP_SAVE = 0, OP_LOAD = 1, OP_DELETE = 2, OP_EDIT_LOAD = 3 };
struct Job {
    int   op;
    char  folder[64];
    char  name[64];
    uint8_t* image;     // owned by the worker for OP_SAVE, else null
    uint32_t imageSize;
};

struct EditLoadResult {
    int error; // 0=ok, 1=missing, 2=bad/read error
    char folder[64];
    char name[64];
    uint8_t* baseImage;
    uint32_t commandCount;
    StateCommand commands[kMaxCommands];
};

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

// procs.bin record layout (generated by tools/gen_procs_bin.py, see
// actor_data/README.md): dense, proc-id-indexed, 64 bytes/record:
//   0x00 u16 id (big-endian); 0x02 proc name[30]; 0x20 friendly name[32].
static const uint32_t kProcRecordSize  = 64u;
static const uint32_t kProcNameOff      = 2u;
static const uint32_t kProcFriendlyOff  = 32u;

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
        : "(invalid)";
}

// Human name (e.g. "Keese"), or "" when the actor has no friendly name yet.
static const char* actorFriendlyName(s16 procId)
{
    return actorProcIdValid(procId)
        ? (const char*)(procs_bin + (uint32_t)procId * kProcRecordSize + kProcFriendlyOff)
        : "";
}

static bool containsIgnoreCase(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0])
        return true;
    for (const char* start = text; *start; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

// Friendly display name for a list/preview: the human name, or the proc name when
// there's no friendly name yet.
static const char* actorDisplayName(s16 procId)
{
    const char* fr = actorFriendlyName(procId);
    return fr[0] ? fr : actorProcName(procId);
}

static bool commandSemanticsValid(const StateCommand& command)
{
    if (command.enabled > 1)
        return false;
    if (command.type == COMMAND_SPAWN_ACTOR) {
        return actorProcIdValid(command.procId) &&
               command.amount >= 1 && command.amount <= kMaxSpawnAmount;
    }
    if (command.type == COMMAND_DELETE_ACTORS) {
        return actorProcIdValid(command.procId) &&
               command.procId != FPCNM_LINK;
    }
    if (command.type != COMMAND_SET_AREA_FLAG ||
        command.flagType > AREA_FLAG_KEY_COUNT)
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

static bool parseStateImage(const uint8_t* image, uint32_t size,
                            StateCommand* outCommands, uint32_t* outCommandCount)
{
    if (outCommandCount)
        *outCommandCount = 0;
    if (!image || size < kBaseFileSize || size > kMaxFileSize)
        return false;

    const StateHeader* state = (const StateHeader*)image;
    if (state->magic != kMagic || state->version != kVersion)
        return false;
    if (!state->stage[0] ||
        !memchr(state->stage, '\0', sizeof(state->stage)) ||
        state->horseRiding > 1 || state->camValid > 1 ||
        !isfinite(state->pos.x) || !isfinite(state->pos.y) ||
        !isfinite(state->pos.z))
        return false;
    if (state->camValid &&
        (!isfinite(state->camAt.x) || !isfinite(state->camAt.y) ||
         !isfinite(state->camAt.z) || !isfinite(state->camEye.x) ||
         !isfinite(state->camEye.y) || !isfinite(state->camEye.z)))
        return false;
    if (state->horseRiding &&
        (!isfinite(state->horsePos.x) || !isfinite(state->horsePos.y) ||
         !isfinite(state->horsePos.z)))
        return false;
    if (size == kBaseFileSize)
        return true;
    if (size < kBaseFileSize + sizeof(CommandSectionHeader))
        return false;

    const CommandSectionHeader* section =
        (const CommandSectionHeader*)(image + kBaseFileSize);
    if (section->magic != kCommandMagic ||
        section->version != kCommandVersion ||
        section->headerSize != sizeof(CommandSectionHeader) ||
        section->commandCount > kMaxCommands ||
        section->totalSize != size - kBaseFileSize ||
        section->totalSize < sizeof(CommandSectionHeader))
        return false;

    const uint32_t payloadSize =
        section->totalSize - sizeof(CommandSectionHeader);
    const uint8_t* payload = image + kBaseFileSize + sizeof(CommandSectionHeader);
    if (crc32(payload, payloadSize) != section->payloadCrc32)
        return false;

    uint32_t offset = kBaseFileSize + sizeof(CommandSectionHeader);
    for (uint32_t i = 0; i < section->commandCount; ++i) {
        if (offset > size || size - offset < sizeof(CommandRecordHeader))
            return false;
        const CommandRecordHeader* record =
            (const CommandRecordHeader*)(image + offset);
        if (record->size < sizeof(CommandRecordHeader) ||
            record->size > size - offset)
            return false;

        StateCommand command = {};
        command.type = record->type;
        command.frameDelay = record->frameDelay & kCommandFrameDelayMask;
        command.enabled = (record->frameDelay & kCommandDisabledFlag) ? 0 : 1;
        command.amount = 1;
        const uint8_t* recordPayload = image + offset + sizeof(*record);

        if (record->type == COMMAND_SPAWN_ACTOR &&
            record->size == kSpawnRecordSize) {
            const SpawnActorPayload* spawn =
                (const SpawnActorPayload*)recordPayload;
            if (spawn->reserved0)
                return false;
            command.procId = spawn->procId;
            command.subtype = spawn->subtype;
            command.params = spawn->params;
            command.position = spawn->position;
            command.angle = spawn->angle;
            command.amount = spawn->amount ? spawn->amount : 1;
            if (!isfinite(command.position.x) ||
                !isfinite(command.position.y) ||
                !isfinite(command.position.z))
                return false;
        } else if (record->type == COMMAND_DELETE_ACTORS &&
                   record->size == kDeleteRecordSize) {
            const DeleteActorsPayload* remove =
                (const DeleteActorsPayload*)recordPayload;
            if (remove->reserved)
                return false;
            command.procId = remove->procId;
        } else if (record->type == COMMAND_SET_AREA_FLAG &&
                   record->size == kAreaFlagRecordSize) {
            const SetAreaFlagPayload* flag =
                (const SetAreaFlagPayload*)recordPayload;
            command.flagType = flag->flagType;
            command.flagValue = flag->value;
            command.flagIndex = flag->index;
        } else {
            return false;
        }

        if (!commandSemanticsValid(command))
            return false;
        if (outCommands)
            outCommands[i] = command;
        offset += record->size;
    }

    if (offset != size)
        return false;
    if (outCommandCount)
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

static uint8_t* buildStateImage(const uint8_t* baseImage,
                                const StateCommand* commands,
                                uint32_t commandCount,
                                uint32_t* outSize)
{
    if (outSize)
        *outSize = 0;
    if (!baseImage || commandCount > kMaxCommands)
        return nullptr;

    uint32_t payloadSize = 0;
    for (uint32_t i = 0; i < commandCount; ++i) {
        if (!commandSemanticsValid(commands[i]))
            return nullptr;
        uint32_t recordSize = commandRecordSize(commands[i]);
        if (!recordSize || payloadSize > kMaxCommandBytes - recordSize)
            return nullptr;
        payloadSize += recordSize;
    }

    uint32_t size = kBaseFileSize;
    if (commandCount)
        size += sizeof(CommandSectionHeader) + payloadSize;
    uint8_t* image = (uint8_t*)memalign(0x40, size);
    if (!image)
        return nullptr;
    memcpy(image, baseImage, kBaseFileSize);
    if (!commandCount) {
        if (outSize)
            *outSize = size;
        return image;
    }

    CommandSectionHeader* section =
        (CommandSectionHeader*)(image + kBaseFileSize);
    section->magic = kCommandMagic;
    section->version = kCommandVersion;
    section->headerSize = sizeof(CommandSectionHeader);
    section->totalSize = sizeof(CommandSectionHeader) + payloadSize;
    section->commandCount = commandCount;
    section->payloadCrc32 = 0;

    uint32_t offset = kBaseFileSize + sizeof(CommandSectionHeader);
    for (uint32_t i = 0; i < commandCount; ++i) {
        const StateCommand& command = commands[i];
        CommandRecordHeader* record = (CommandRecordHeader*)(image + offset);
        record->type = command.type;
        record->size = (uint16_t)commandRecordSize(command);
        record->frameDelay = (command.frameDelay & kCommandFrameDelayMask) |
                             (command.enabled ? 0u : kCommandDisabledFlag);
        uint8_t* payload = image + offset + sizeof(*record);

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

    uint8_t* payload = image + kBaseFileSize + sizeof(CommandSectionHeader);
    section->payloadCrc32 = crc32(payload, payloadSize);
    if (outSize)
        *outSize = size;
    return image;
}

static OSThread       s_thread;
static bool           s_threadStarted = false;
static OSMessageQueue s_queue;
static OSMessage      s_msgBuf[8];
static __attribute__((aligned(16))) uint8_t s_stack[16 * 1024];

// Load result handed from worker -> present thread (ready flag set last).
static StateHeader    s_loadHeader;
static uint8_t*       s_pendingInfo = nullptr;   // malloc'd save block (worker -> present)
static volatile bool  s_loadReady   = false;
static volatile bool  s_loadBusy    = false;
static volatile int   s_loadError   = 0;         // 1=missing, 2=bad/read error
static char           s_readyLoadFolder[64] = {};
static char           s_readyLoadName[64] = {};
static bool           s_applyPosNext = true;     // override Link's pos on this load?
static bool           s_pendingSerialized = false; // pendingInfo starts as a 0xDF8 image
static bool           s_normalSaveResume = false; // use file-select transition semantics
static StateCommand   s_loadCommands[kMaxCommands] = {};
static uint32_t       s_loadCommandCount = 0;

// Edit result handed from worker -> present thread.
static EditLoadResult* s_editResult = nullptr;
static volatile bool   s_editReady = false;
static volatile bool   s_editBusy = false;

// Pending restore/override applied on the present thread once the warp loads.
//
// Phases: TEARDOWN waits for the old scene to drop Link (pMPlayer0 null) and
// injects the saved block then -- after the old scene cleaned up with its own
// data, before the new scene builds (tpgz's phase_1-inject timing) -> WAIT for the
// new Link, then immediately re-stamp + apply the saved position -> APPLY pins the
// position (and camera) until Link's collision reports ground, so he doesn't fall
// through a floor still streaming in. Room + velocity are corrected on the teleport.
enum { OV_IDLE = 0, OV_TEARDOWN, OV_WAIT, OV_APPLY };
static int          s_ovPhase = OV_IDLE;
static int          s_ovWait  = 0;
static int          s_ovApply = 0;       // hold-phase timeout countdown
static int          s_ovGround = 0;      // consecutive grounded frames seen
static char         s_ovStage[8] = {};
static cXyz         s_ovPos = {};
static s16          s_ovAngle = 0;
static s8           s_ovRoom  = 0;
static cXyz         s_ovCamAt = {};
static cXyz         s_ovCamEye = {};
static bool         s_ovCamValid = false;
static cXyz         s_ovHorsePos = {};
static s16          s_ovHorseAngle = 0;
static s8           s_ovHorseRoom = 0;
static bool         s_ovHorseRiding = false;
static bool         s_ovHorseSpawnRequested = false;
static int          s_ovHorseWait = 0;
static int          s_ovReadyFrames = 0;       // consecutive target-stage-ready frames
static bool         s_ovApplyPos   = true;      // force Link to s_ovPos once settled?
static bool         s_ovNeedsRuntimePrep = false; // load requested before gameplay HUD/items exist
static bool         s_ovCommandsStarted = false;

// Commands continue after the load-position settle phase has completed.
static StateCommand s_execCommands[kMaxCommands] = {};
static uint32_t     s_execCommandCount = 0;
static uint32_t     s_execCommandIndex = 0;
static uint32_t     s_execFrame = 0;
static char         s_execStage[8] = {};

static const int OV_TEARDOWN_TIMEOUT = 30;   // frames to wait for the old scene to drop Link
static const int OV_PERSONAL_TEARDOWN_TIMEOUT = 300; // never force-inject a Personal save
static const int OV_HOLD_TIMEOUT   = 90;  // max frames (~10s) to wait for the floor to stream in
static const int OV_GROUND_CONFIRM = 3;    // frames of confirmed ground contact before releasing
static const int OV_READY_CONFIRM  = 3;    // target stage + Link stable before applying state
static const int OV_HORSE_SPAWN_GRACE = 30; // let a stage-provided Epona finish creating first
// Mounted Link never reports foot-ground contact (he sits ~230u above Epona), so
// we can't use the on-ground settle the foot path uses. Instead we re-pin Epona
// every frame for a fixed window long enough to outlast the load wipe (~26
// frames) and the room-entry positioning that runs as the new scene settles --
// otherwise the entry logic snaps Epona to the stage spawn point right after a
// too-short hold, dropping us at the entrance instead of the saved spot.
static const int OV_HORSE_HOLD_FRAMES = 30;

// Synchronous crash breadcrumbs for raw Personal save loads. These are dormant
// for normal save-state loads. Each line reaches log.txt before execution
// continues, so the final line identifies the call/phase that did not return.
#ifdef TPHD_TOOLS_DEBUG
static uint32_t s_gameSaveTraceCounter = 0;
#endif
static uint32_t s_activeGameSaveTrace = 0;
static char     s_gameSaveTraceSource[64] = {};
static bool     s_traceWaitEntryLogged = false;
static bool     s_traceTargetReadyLogged = false;

static void copyPrintableFixed(char* out, size_t outSize, const char* src, size_t srcSize)
{
    if (!out || outSize == 0)
        return;
    size_t n = srcSize;
    if (n > outSize - 1)
        n = outSize - 1;
    size_t i = 0;
    for (; i < n && src[i]; ++i) {
        unsigned char c = (unsigned char)src[i];
        out[i] = isprint(c) ? (char)c : '?';
    }
    out[i] = '\0';
}

#ifdef TPHD_TOOLS_DEBUG
static void formatHexBytes(char* out, size_t outSize, const uint8_t* bytes, int count)
{
    if (!out || outSize == 0)
        return;
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; bytes && i < count && used + 4 < outSize; ++i) {
        int n = snprintf(out + used, outSize - used, "%s%02X",
                         i ? " " : "", (unsigned)bytes[i]);
        if (n <= 0 || (size_t)n >= outSize - used)
            break;
        used += (size_t)n;
    }
}
#endif

static void logGameSaveImageState(const char* point, const uint8_t* image, uint32_t size)
{
#ifndef TPHD_TOOLS_DEBUG
    (void)point;
    (void)image;
    (void)size;
    return;
#else
    if (!s_activeGameSaveTrace)
        return;
    if (!image) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] %s: image=null",
            (unsigned)s_activeGameSaveTrace, point);
        return;
    }

    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: image=%p size=0x%X aligned40=%d source='%s'",
        (unsigned)s_activeGameSaveTrace, point, (const void*)image, (unsigned)size,
        (((uintptr_t)image & 0x3Fu) == 0), s_gameSaveTraceSource);

    if (size < GAME_DSV_SERIALIZED_SIZE) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] %s: image too small for state dump",
            (unsigned)s_activeGameSaveTrace, point);
        return;
    }

    dSv_status_a_c status;
    dSv_item_c items;
    dSv_getItem_c gotItems;
    dSv_itemRecord_c records;
    dSv_itemMax_c maximums;
    dSv_player_return_place_c destination;
    memcpy(&status, image, sizeof(status));
    memcpy(&items, image + 0x9C, sizeof(items));
    memcpy(&gotItems, image + 0xCC, sizeof(gotItems));
    memcpy(&records, image + 0xEC, sizeof(records));
    memcpy(&maximums, image + 0xF8, sizeof(maximums));
    memcpy(&destination, image + DSV_DAT_RETURN_PLACE_OFF, sizeof(destination));

    char destinationStage[9];
    char lastStayStage[9];
    copyPrintableFixed(destinationStage, sizeof(destinationStage),
                       destination.mName, sizeof(destination.mName));
    copyPrintableFixed(lastStayStage, sizeof(lastStayStage),
                       (const char*)image + DSV_DAT_LASTSTAY_OFF + DSV_LASTSTAY_OFF_NAME, 8);
    const s8 lastStayRoom =
        *(const s8*)(image + DSV_DAT_LASTSTAY_OFF + DSV_LASTSTAY_OFF_ROOM);

    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: destination='%s' room=%d spawn=%u "
        "lastStay='%s' room=%d",
        (unsigned)s_activeGameSaveTrace, point, destinationStage,
        (int)destination.mRoomNo, (unsigned)destination.mPlayerStatus,
        lastStayStage, (int)lastStayRoom);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: life=%u/%u rupees=%u oil=%u/%u "
        "wallet=%u transform=%u select=%02X,%02X,%02X,%02X "
        "equip=%02X,%02X,%02X,%02X,%02X,%02X",
        (unsigned)s_activeGameSaveTrace, point,
        (unsigned)status.mLife, (unsigned)status.mMaxLife, (unsigned)status.mRupee,
        (unsigned)status.mOil, (unsigned)status.mMaxOil, (unsigned)status.mWalletSize,
        (unsigned)status.mTransformStatus,
        (unsigned)status.mSelectItem[0], (unsigned)status.mSelectItem[1],
        (unsigned)status.mSelectItem[2], (unsigned)status.mSelectItem[3],
        (unsigned)status.mSelectEquip[0], (unsigned)status.mSelectEquip[1],
        (unsigned)status.mSelectEquip[2], (unsigned)status.mSelectEquip[3],
        (unsigned)status.mSelectEquip[4], (unsigned)status.mSelectEquip[5]);

    char hex[160];
    formatHexBytes(hex, sizeof(hex), items.mItems, 12);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: items[00..11]=%s",
        (unsigned)s_activeGameSaveTrace, point, hex);
    formatHexBytes(hex, sizeof(hex), items.mItems + 12, 12);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: items[12..23]=%s",
        (unsigned)s_activeGameSaveTrace, point, hex);
    formatHexBytes(hex, sizeof(hex), items.mItemSlots, 12);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: slots[00..11]=%s",
        (unsigned)s_activeGameSaveTrace, point, hex);
    formatHexBytes(hex, sizeof(hex), items.mItemSlots + 12, 12);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: slots[12..23]=%s",
        (unsigned)s_activeGameSaveTrace, point, hex);

    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: counts arrows=%u bombs=%u,%u,%u "
        "bottles=%u,%u,%u,%u seeds=%u max=%u,%u,%u,%u,%u,%u,%u,%u",
        (unsigned)s_activeGameSaveTrace, point,
        (unsigned)records.mArrowNum,
        (unsigned)records.mBombNum[0], (unsigned)records.mBombNum[1],
        (unsigned)records.mBombNum[2],
        (unsigned)records.mBottleNum[0], (unsigned)records.mBottleNum[1],
        (unsigned)records.mBottleNum[2], (unsigned)records.mBottleNum[3],
        (unsigned)records.mPachinkoNum,
        (unsigned)maximums.mItemMax[0], (unsigned)maximums.mItemMax[1],
        (unsigned)maximums.mItemMax[2], (unsigned)maximums.mItemMax[3],
        (unsigned)maximums.mItemMax[4], (unsigned)maximums.mItemMax[5],
        (unsigned)maximums.mItemMax[6], (unsigned)maximums.mItemMax[7]);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] %s: gotFlags=%08X,%08X,%08X,%08X,"
        "%08X,%08X,%08X,%08X",
        (unsigned)s_activeGameSaveTrace, point,
        (unsigned)gotItems.mItemFlags[0], (unsigned)gotItems.mItemFlags[1],
        (unsigned)gotItems.mItemFlags[2], (unsigned)gotItems.mItemFlags[3],
        (unsigned)gotItems.mItemFlags[4], (unsigned)gotItems.mItemFlags[5],
        (unsigned)gotItems.mItemFlags[6], (unsigned)gotItems.mItemFlags[7]);
#endif
}

static void logRoomState(const char* point, s8 targetRoom, const void* pendingInfo,
                         bool pendingRestartValid)
{
#ifndef TPHD_TOOLS_DEBUG
    (void)point;
    (void)targetRoom;
    (void)pendingInfo;
    (void)pendingRestartValid;
    return;
#else
    fopAc_ac_c* link = dComIfGp_getPlayer();
    char stage[9];
    copyPrintableFixed(stage, sizeof(stage), dStage_getStageName(), 8);

    TPHD_BREADCRUMB(
        "[tphd_tools][state-room] %s: stage='%s' target=%d start=%d next=%d "
        "warpPending=%d link=%p actorRooms(home/old/current)=%d/%d/%d",
        point, stage, (int)targetRoom, (int)dStage_getStartRoomNo(),
        (int)dStage_getNextRoomNo(), dStage_warpPending(), (void*)link,
        link ? (int)link->home.roomNo : -1,
        link ? (int)link->old.roomNo : -1,
        link ? (int)link->current.roomNo : -1);

    volatile dSv_restart_c* live = dSv_getRestart();
    TPHD_BREADCRUMB(
        "[tphd_tools][state-room] %s: liveRestart room=%d spawn=%d "
        "pos=(%.1f,%.1f,%.1f) angle=%d param=%08X mode=%08X",
        point, (int)live->mRoomNo, (int)live->mStartPoint,
        live->mRoomPos.x, live->mRoomPos.y, live->mRoomPos.z,
        (int)live->mRoomAngleY, (unsigned)live->mRoomParam,
        (unsigned)live->mLastMode);

    if (pendingInfo && pendingRestartValid) {
        const dSv_restart_c* pending = dSv_getRestartFromInfo(pendingInfo);
        TPHD_BREADCRUMB(
            "[tphd_tools][state-room] %s: pendingRestart room=%d spawn=%d "
            "pos=(%.1f,%.1f,%.1f) angle=%d param=%08X mode=%08X",
            point, (int)pending->mRoomNo, (int)pending->mStartPoint,
            pending->mRoomPos.x, pending->mRoomPos.y, pending->mRoomPos.z,
            (int)pending->mRoomAngleY, (unsigned)pending->mRoomParam,
            (unsigned)pending->mLastMode);
    }
#endif
}

// ---- cached file list (present thread, on demand) ---------------------------
static Storage::StateEntry s_files[64];
static Storage::StateFolder s_folders[5];
static int       s_folderCount = 0;
static int       s_selectedTab = 0;       // 0=Main, 1..5=discovered folder
static bool      s_foldersLoaded = false;
static int       s_fileCount  = 0;
static bool      s_listLoaded = false;
static int       s_refreshIn  = 0;      // frames until an auto-refresh (after a save)
static char      s_status[96] = "";

// ---- UI state ---------------------------------------------------------------
static bool s_enabled = false;
static bool s_overridePosition = true;
static bool s_reloadLastHotkey = false;
static char s_lastLoadedFolder[64] = {};
static char s_lastLoadedName[64] = {};
static char s_nameBuf[64] = "";
static int  s_confirmDelete = -1;       // index pending delete confirmation
static bool s_openDeletePopup = false;
static bool s_editorOpen = false;
static uint8_t* s_editorBaseImage = nullptr;
static StateCommand s_editorCommands[kMaxCommands] = {};
static uint32_t s_editorCommandCount = 0;
static char s_editorFolder[64] = {};
static char s_editorName[64] = {};
static StateCommand s_editorForm = {};
static int s_editorFormIndex = -1;
static char s_editorActorQuery[64] = {};
static char s_editorStatus[128] = {};

void DrawMenuItem()     { ImGui::Checkbox("Save Loader", &s_enabled); }
bool IsEnabled()        { return s_enabled; }
void SetEnabled(bool e) { s_enabled = e; }
bool IsPositionOverrideEnabled() { return s_overridePosition; }
void SetPositionOverrideEnabled(bool e) { s_overridePosition = e; }
bool IsReloadLastHotkeyEnabled() { return s_reloadLastHotkey; }
void SetReloadLastHotkeyEnabled(bool e) { s_reloadLastHotkey = e; }
const char* GetLastLoadedStateName() { return s_lastLoadedName; }
void SetLastLoadedStateName(const char* name)
{
    if (!name)
        name = "";
    strncpy(s_lastLoadedName, name, sizeof(s_lastLoadedName) - 1);
    s_lastLoadedName[sizeof(s_lastLoadedName) - 1] = '\0';
}
const char* GetLastLoadedStateFolder() { return s_lastLoadedFolder; }
void SetLastLoadedStateFolder(const char* folder)
{
    if (!folder)
        folder = "";
    strncpy(s_lastLoadedFolder, folder, sizeof(s_lastLoadedFolder) - 1);
    s_lastLoadedFolder[sizeof(s_lastLoadedFolder) - 1] = '\0';
}

// ---- worker -----------------------------------------------------------------
static int worker(int argc, const char** argv)
{
    (void)argc; (void)argv;

    for (;;) {
        OSMessage msg;
        OSReceiveMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        Job* job = (Job*)msg.message;
        if (!job)
            continue;

        if (job->op == OP_SAVE) {
            if (Storage::SaveState(job->folder, job->name,
                                   job->image, job->imageSize))
                Logger::Log("[tphd_tools] state saved: %s/%s",
                            job->folder[0] ? job->folder : "Main", job->name);
            else
                Logger::Log("[tphd_tools] state save failed: %s/%s",
                            job->folder[0] ? job->folder : "Main", job->name);
            free(job->image);
        } else if (job->op == OP_DELETE) {
            if (Storage::DeleteState(job->folder, job->name))
                Logger::Log("[tphd_tools] state deleted: %s/%s",
                            job->folder[0] ? job->folder : "Main", job->name);
            else
                Logger::Log("[tphd_tools] state delete failed: %s/%s",
                            job->folder[0] ? job->folder : "Main", job->name);
        } else if (job->op == OP_EDIT_LOAD) {
            EditLoadResult* result =
                (EditLoadResult*)calloc(1, sizeof(EditLoadResult));
            if (result) {
                snprintf(result->folder, sizeof(result->folder), "%.63s", job->folder);
                snprintf(result->name, sizeof(result->name), "%.63s", job->name);

                uint8_t* buf = (uint8_t*)memalign(0x40, kMaxFileSize + 1u);
                uint32_t n = 0;
                Storage::ReadResult r =
                    buf ? Storage::LoadState(job->folder, job->name, buf,
                                             kMaxFileSize + 1u, &n)
                        : Storage::READ_ERROR;
                if (r == Storage::READ_OK &&
                    parseStateImage(buf, n, result->commands,
                                    &result->commandCount)) {
                    result->baseImage = (uint8_t*)memalign(0x40, kBaseFileSize);
                    if (result->baseImage)
                        memcpy(result->baseImage, buf, kBaseFileSize);
                    else
                        result->error = 2;
                } else {
                    result->error = r == Storage::READ_MISSING ? 1 : 2;
                }
                free(buf);
                s_editResult = result;
                s_editReady = true; // set LAST
            } else {
                s_editBusy = false;
            }
        } else {  // OP_LOAD
            bool queued = false;
            uint8_t* buf = (uint8_t*)memalign(0x40, kMaxFileSize + 1u);
            uint32_t n = 0;
            Storage::ReadResult r = buf ? Storage::LoadState(job->folder, job->name, buf,
                                                             kMaxFileSize + 1u, &n)
                                        : Storage::READ_ERROR;
            const StateHeader* hdr = (const StateHeader*)buf;
            StateCommand commands[kMaxCommands] = {};
            uint32_t commandCount = 0;
            if (r == Storage::READ_OK &&
                parseStateImage(buf, n, commands, &commandCount)) {
                uint8_t* info = (uint8_t*)memalign(0x40, kInfoSize);
                if (info) {
                    memcpy(info, buf + sizeof(StateHeader), kInfoSize);
                    s_loadHeader  = *hdr;
                    s_pendingInfo = info;
                    memcpy(s_loadCommands, commands,
                           commandCount * sizeof(StateCommand));
                    s_loadCommandCount = commandCount;
                    strncpy(s_readyLoadFolder, job->folder, sizeof(s_readyLoadFolder) - 1);
                    s_readyLoadFolder[sizeof(s_readyLoadFolder) - 1] = '\0';
                    strncpy(s_readyLoadName, job->name, sizeof(s_readyLoadName) - 1);
                    s_readyLoadName[sizeof(s_readyLoadName) - 1] = '\0';
                    s_loadReady   = true;   // set LAST
                    queued = true;
                    Logger::Log("[tphd_tools] state load queued: %s/%s",
                                job->folder[0] ? job->folder : "Main", job->name);
                } else {
                    s_loadError = 2;
                    Logger::Log("[tphd_tools] state load: allocation failed for %s", job->name);
                }
            } else if (r == Storage::READ_MISSING) {
                s_loadError = 1;
                Logger::Log("[tphd_tools] state load: missing %s", job->name);
            } else {
                s_loadError = 2;
                Logger::Log("[tphd_tools] state load: invalid v8 file %s (n=%u)",
                            job->name, (unsigned)n);
            }
            if (!queued)
                s_loadBusy = false;
            free(buf);
        }
        free(job);
    }
    return 0;  // not reached
}

static void startWorker()
{
    if (s_threadStarted)
        return;
    OSInitMessageQueue(&s_queue, s_msgBuf, (int32_t)(sizeof(s_msgBuf) / sizeof(s_msgBuf[0])));
    void* stackTop = s_stack + sizeof(s_stack);
    if (!OSCreateThread(&s_thread, worker, 0, nullptr, stackTop, sizeof(s_stack), 16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        Logger::Log("[tphd_tools] save-state worker create failed");
        return;
    }
    OSSetThreadName(&s_thread, "tphd_tools_state");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
}

static bool postJob(Job* job)
{
    startWorker();
    if (!s_threadStarted) {
        if (job->image) free(job->image);
        free(job);
        return false;
    }
    OSMessage msg;
    msg.message = job;
    msg.args[0] = msg.args[1] = msg.args[2] = 0;
    if (!OSSendMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        if (job->image) free(job->image);
        free(job);   // queue full -- drop
        return false;
    }
    return true;
}

// ---- save (present thread) --------------------------------------------------
// Turn a user-typed name into a safe filename stem; empty -> timestamp.
static void sanitizeName(const char* in, char* out, int outSize)
{
    int j = 0;
    for (int i = 0; in[i] && j < outSize - 1; ++i) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            out[j++] = c;
        else if (c == ' ')
            out[j++] = '_';
    }
    out[j] = '\0';
    if (j == 0) {
        OSCalendarTime ct;
        OSTicksToCalendarTime(OSGetTime(), &ct);
        snprintf(out, outSize, "state_%04d%02d%02d_%02d%02d%02d", ct.tm_year, ct.tm_mon + 1,
                 ct.tm_mday, ct.tm_hour, ct.tm_min, ct.tm_sec);
    }
}

static const char* selectedFolder()
{
    if (s_selectedTab <= 0 || s_selectedTab > s_folderCount)
        return "";
    return s_folders[s_selectedTab - 1].name;
}

static void copyJobFolder(Job* job, const char* folder)
{
    if (!folder)
        folder = "";
    snprintf(job->folder, sizeof(job->folder), "%.63s", folder);
}

static void requestSave(const char* folder, const char* name)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link) {
        snprintf(s_status, sizeof(s_status), "Can't save: Link not loaded");
        return;
    }
    uint8_t* image = (uint8_t*)memalign(0x40, kBaseFileSize);
    if (!image)
        return;

    StateHeader* hdr = (StateHeader*)image;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic   = kMagic;
    hdr->version = kVersion;
    const char* sn = dStage_getStageName();
    for (int i = 0; i < (int)sizeof(hdr->stage) - 1 && sn[i]; ++i)
        hdr->stage[i] = sn[i];
    hdr->room  = dStage_getRoomNo();
    hdr->layer = dStage_getLayer();
    hdr->spawn = dStage_getSpawn();
    hdr->pos   = link->current.pos;
    hdr->angle = link->shape_angle.y;
    fopAc_ac_c* horse = dComIfGp_getHorseActor();
    if (horse && dPlayer_isHorseRiding(link)) {
        hdr->horsePos    = horse->current.pos;
        hdr->horseAngle  = horse->shape_angle.y;
        hdr->horseRoom   = horse->current.roomNo;
        hdr->horseRiding = 1;
    }
    // Capture the camera transform so the load returns to the same view.
    CameraXform* cam = dCam_getXform();
    if (cam) {
        hdr->camAt    = cam->at;
        hdr->camEye   = cam->eye;
        hdr->camValid = 1;
    }
    // Snapshot the whole persistent save block (player status, items, events,
    // area flags, restart -- everything the game serializes on a save).
    uint8_t* infoImage = image + sizeof(StateHeader);
    memcpy(infoImage, (const void*)GAME_ADDR_gameInfo_info, kInfoSize);

    // mLastMode in the persistent block describes the most recent transition,
    // so it is often stale while Link is standing in a room. Rebuild it from
    // the live Link using the same packer dComIfGp_setNextStage calls. The high
    // byte carries the held-item/action start state; the copied info block
    // already contains the corresponding button assignments and inventory.
    dSv_restart_c* capturedRestart = dSv_getRestartFromInfo(infoImage);
    const uint32_t oldRestartMode = capturedRestart->mLastMode;
    capturedRestart->mLastMode = dStage_captureRestartMode(link);
    Logger::Log("[tphd_tools] captured live restart mode: %08X -> %08X",
                (unsigned)oldRestartMode, (unsigned)capturedRestart->mLastMode);
    logRoomState("capture", hdr->room, infoImage, true);

    char stem[64];
    sanitizeName(name, stem, sizeof(stem));

    Job* job = (Job*)malloc(sizeof(Job));
    if (!job) { free(image); return; }
    job->op    = OP_SAVE;
    job->image = image;
    job->imageSize = kBaseFileSize;
    copyJobFolder(job, folder);
    snprintf(job->name, sizeof(job->name), "%.63s", stem);

    Logger::Log("[tphd_tools] state save queued: %s/%s (mounted=%u)",
                job->folder[0] ? job->folder : "Main", stem, (unsigned)hdr->horseRiding);
    postJob(job);
    snprintf(s_status, sizeof(s_status), "Saving %s ...", stem);
    s_refreshIn = 30;   // auto-refresh the list ~1s after the async write
}

static void requestLoad(const char* folder, const char* name)
{
    if (!name || !name[0]) {
        snprintf(s_status, sizeof(s_status), "No previously loaded state.");
        return;
    }
    if (s_loadBusy || s_loadReady || s_ovPhase != OV_IDLE) {
        snprintf(s_status, sizeof(s_status), "A state load is already in progress.");
        return;
    }
    Job* job = (Job*)malloc(sizeof(Job));
    if (!job)
        return;
    job->op    = OP_LOAD;
    job->image = nullptr;
    job->imageSize = 0;
    copyJobFolder(job, folder);
    snprintf(job->name, sizeof(job->name), "%.63s", name);
    s_applyPosNext = s_overridePosition;
    s_loadBusy = true;
    s_loadError = 0;
    s_loadCommandCount = 0;
    char location[132];
    snprintf(location, sizeof(location), "%s%s%s",
             job->folder[0] ? job->folder : "", job->folder[0] ? "/" : "", name);
    if (postJob(job))
        snprintf(s_status, sizeof(s_status), "Loading %.82s ...", location);
    else {
        s_loadBusy = false;
        snprintf(s_status, sizeof(s_status), "Unable to queue state load.");
    }
}

// Reload `stage` rebuilding the runtime from the CURRENT in-memory info block
// (no file). Used by the Debug Save Loader after it applies a .dat into info.
// pos != null forces Link there; null lets the stage's spawn point place him.
void BeginInPlaceReload(const char* stage, s8 room, s16 spawn, s8 layer,
                        const cXyz* pos, s16 angle)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return;
    uint8_t* snap = (uint8_t*)memalign(0x40, kInfoSize);
    if (!snap)
        return;
    memcpy(snap, (const void*)GAME_ADDR_gameInfo_info, kInfoSize);
    if (s_pendingInfo)
        free(s_pendingInfo);
    s_pendingInfo = snap;

    memset(&s_loadHeader, 0, sizeof(s_loadHeader));
    for (int i = 0; i < (int)sizeof(s_loadHeader.stage) - 1 && stage[i]; ++i)
        s_loadHeader.stage[i] = stage[i];
    s_loadHeader.room  = room;
    s_loadHeader.layer = layer;
    s_loadHeader.spawn = spawn;
    if (pos) {
        s_loadHeader.pos   = *pos;
        s_loadHeader.angle = angle;
    } else {
        s_loadHeader.pos   = link->current.pos;
        s_loadHeader.angle = link->shape_angle.y;
    }
    s_applyPosNext = (pos != nullptr);
    s_readyLoadFolder[0] = '\0';
    s_readyLoadName[0] = '\0'; // debug-save reloads are not named save states
    s_pendingSerialized = false;
    s_normalSaveResume = false;
    s_loadCommandCount = 0;
    s_loadReady = true;        // Tick() drives the warp + re-stamp
}

bool BeginGameSaveLoad(const char* sourceName, const void* image, uint32_t size)
{
#ifdef TPHD_TOOLS_DEBUG
    const uint32_t trace = s_gameSaveTraceCounter + 1;
#else
    (void)sourceName;
#endif
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] bridge request: source='%s' image=%p size=0x%X "
        "loadBusy=%d loadReady=%d phase=%d pendingInfo=%p",
        (unsigned)trace, sourceName ? sourceName : "(null)", image, (unsigned)size,
        s_loadBusy, s_loadReady, s_ovPhase, (void*)s_pendingInfo);

    if (!image || size < GAME_DSV_SERIALIZED_SIZE ||
        s_loadBusy || s_loadReady || s_ovPhase != OV_IDLE) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] bridge rejected: image=%d sizeOk=%d "
            "loadBusy=%d loadReady=%d phase=%d",
            (unsigned)trace, image != nullptr, size >= GAME_DSV_SERIALIZED_SIZE,
            s_loadBusy, s_loadReady, s_ovPhase);
        return false;
    }

    const uint8_t* bytes = (const uint8_t*)image;
    const dSv_player_return_place_c* destination =
        (const dSv_player_return_place_c*)(bytes + DSV_DAT_RETURN_PLACE_OFF);
    if (!destination->mName[0]) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] bridge rejected: empty destination stage",
            (unsigned)trace);
        return false;
    }

#ifdef TPHD_TOOLS_DEBUG
    s_gameSaveTraceCounter = trace;
    s_activeGameSaveTrace = trace;
    snprintf(s_gameSaveTraceSource, sizeof(s_gameSaveTraceSource), "%s",
             sourceName && sourceName[0] ? sourceName : "(unnamed)");
    s_traceWaitEntryLogged = false;
    s_traceTargetReadyLogged = false;
#endif
    logGameSaveImageState("source-image", bytes, size);

    uint8_t* copy = (uint8_t*)memalign(0x40, kInfoSize);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] bridge copy allocation: ptr=%p size=0x%X aligned40=%d",
        (unsigned)trace, (void*)copy, (unsigned)kInfoSize,
        copy && (((uintptr_t)copy & 0x3Fu) == 0));
    if (!copy) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] bridge rejected: copy allocation failed",
            (unsigned)trace);
        s_activeGameSaveTrace = 0;
        s_gameSaveTraceSource[0] = '\0';
        return false;
    }
    memset(copy, 0, kInfoSize);
    memcpy(copy, image, GAME_DSV_SERIALIZED_SIZE);
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] bridge image copied into pending buffer",
        (unsigned)trace);

    if (s_pendingInfo) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] bridge freeing stale pendingInfo=%p",
            (unsigned)trace, (void*)s_pendingInfo);
        free(s_pendingInfo);
    }
    s_pendingInfo = copy;
    memset(&s_loadHeader, 0, sizeof(s_loadHeader));
    memcpy(s_loadHeader.stage, destination->mName, sizeof(s_loadHeader.stage));
    s_loadHeader.stage[sizeof(s_loadHeader.stage) - 1] = '\0';
    s_loadHeader.spawn = destination->mPlayerStatus;
    s_loadHeader.room = destination->mRoomNo;
    s_loadHeader.layer = DSTAGE_LAYER_DEFAULT;

    s_applyPosNext = false;
    s_readyLoadFolder[0] = '\0';
    s_readyLoadName[0] = '\0';
    s_pendingSerialized = true;
    s_normalSaveResume = true;
    s_loadCommandCount = 0;
    s_loadReady = true;
    TPHD_BREADCRUMB(
        "[tphd_tools][personal:%u] bridge accepted: pendingInfo=%p stage='%s' "
        "room=%d spawn=%d serialized=%d normalResume=%d loadReady=%d",
        (unsigned)trace, (void*)s_pendingInfo, s_loadHeader.stage, (int)s_loadHeader.room,
        (int)s_loadHeader.spawn, s_pendingSerialized, s_normalSaveResume, s_loadReady);
    return true;
}

static void requestDelete(const char* folder, const char* name)
{
    Job* job = (Job*)malloc(sizeof(Job));
    if (!job)
        return;
    job->op    = OP_DELETE;
    job->image = nullptr;
    job->imageSize = 0;
    copyJobFolder(job, folder);
    snprintf(job->name, sizeof(job->name), "%.63s", name);
    postJob(job);
    s_refreshIn = 30;
}

static void resetEditorForm(uint16_t type)
{
    memset(&s_editorForm, 0, sizeof(s_editorForm));
    s_editorForm.type = type;
    s_editorForm.procId = -1;      // no actor selected yet (guarded on add)
    s_editorForm.subtype = -1;
    s_editorForm.flagType = AREA_FLAG_SWITCH;
    s_editorForm.flagValue = 1;
    s_editorForm.amount = 1;
    s_editorForm.enabled = 1;
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (link) {
        s_editorForm.position = link->current.pos;
        s_editorForm.angle.y = link->shape_angle.y;
    }
    s_editorActorQuery[0] = '\0';  // the query is a filter only
    s_editorFormIndex = -1;
}

static void requestEditLoad(const char* folder, const char* name)
{
    if (s_editBusy) {
        snprintf(s_editorStatus, sizeof(s_editorStatus),
                 "Another state is already being opened.");
        return;
    }

    Job* job = (Job*)malloc(sizeof(Job));
    if (!job)
        return;
    memset(job, 0, sizeof(*job));
    job->op = OP_EDIT_LOAD;
    copyJobFolder(job, folder);
    snprintf(job->name, sizeof(job->name), "%.63s", name);

    if (s_editorBaseImage) {
        free(s_editorBaseImage);
        s_editorBaseImage = nullptr;
    }
    s_editorCommandCount = 0;
    s_editorOpen = true;
    s_editBusy = true;
    snprintf(s_editorFolder, sizeof(s_editorFolder), "%.63s",
             folder ? folder : "");
    snprintf(s_editorName, sizeof(s_editorName), "%.63s", name);
    snprintf(s_editorStatus, sizeof(s_editorStatus),
             "Opening %.100s ...", name);
    if (!postJob(job)) {
        s_editBusy = false;
        snprintf(s_editorStatus, sizeof(s_editorStatus),
                 "Unable to queue the editor load.");
    }
}

static void consumeEditResult()
{
    if (!s_editReady)
        return;
    s_editReady = false;
    EditLoadResult* result = s_editResult;
    s_editResult = nullptr;
    s_editBusy = false;
    if (!result)
        return;

    if (result->error || !result->baseImage) {
        snprintf(s_editorStatus, sizeof(s_editorStatus), "%s",
                 result->error == 1
                    ? "State file no longer exists."
                    : "State uses an invalid or unsupported v8 format.");
        free(result->baseImage);
        free(result);
        return;
    }

    if (!s_editorOpen) {
        free(result->baseImage);
        free(result);
        return;
    }

    if (s_editorBaseImage)
        free(s_editorBaseImage);
    s_editorBaseImage = result->baseImage;
    result->baseImage = nullptr;
    s_editorCommandCount = result->commandCount;
    memcpy(s_editorCommands, result->commands,
           result->commandCount * sizeof(StateCommand));
    snprintf(s_editorFolder, sizeof(s_editorFolder), "%.63s", result->folder);
    snprintf(s_editorName, sizeof(s_editorName), "%.63s", result->name);
    snprintf(s_editorStatus, sizeof(s_editorStatus),
             "%u command(s)", (unsigned)s_editorCommandCount);
    resetEditorForm(COMMAND_SPAWN_ACTOR);
    free(result);
}

// Serialize the current editor command list and queue an async write of the same
// state file. Keeps the editor open and the base image owned; returns false (with
// s_editorStatus set) on failure. okStatus, if set, is shown in the editor.
static bool persistEditorCommands(const char* okStatus)
{
    if (!s_editorBaseImage) {
        snprintf(s_editorStatus, sizeof(s_editorStatus),
                 "The state has not finished loading.");
        return false;
    }

    uint32_t imageSize = 0;
    uint8_t* image = buildStateImage(
        s_editorBaseImage, s_editorCommands,
        s_editorCommandCount, &imageSize);
    if (!image) {
        snprintf(s_editorStatus, sizeof(s_editorStatus),
                 "Unable to serialize the command list.");
        return false;
    }

    Job* job = (Job*)malloc(sizeof(Job));
    if (!job) {
        free(image);
        return false;
    }
    memset(job, 0, sizeof(*job));
    job->op = OP_SAVE;
    job->image = image;
    job->imageSize = imageSize;
    copyJobFolder(job, s_editorFolder);
    snprintf(job->name, sizeof(job->name), "%.63s", s_editorName);
    if (!postJob(job)) {   // postJob freed image+job on failure
        snprintf(s_editorStatus, sizeof(s_editorStatus),
                 "Unable to queue the updated state.");
        return false;
    }

    if (okStatus)
        snprintf(s_editorStatus, sizeof(s_editorStatus), "%s", okStatus);
    s_refreshIn = 30;
    return true;
}

static void saveEditorCommands()
{
    if (!persistEditorCommands(nullptr))
        return;
    snprintf(s_status, sizeof(s_status),
             "Saving commands for %.63s ...", s_editorName);
    s_editorOpen = false;
    free(s_editorBaseImage);
    s_editorBaseImage = nullptr;
    s_editorCommandCount = 0;
}

// ---- directory listing (present thread, on demand) --------------------------
static void refreshList()
{
    s_fileCount  = Storage::ListStates(selectedFolder(), s_files,
                                       (int)(sizeof(s_files) / sizeof(s_files[0])));
    s_listLoaded = true;
}

static void refreshFolders()
{
    char selected[64];
    strncpy(selected, selectedFolder(), sizeof(selected) - 1);
    selected[sizeof(selected) - 1] = '\0';

    s_folderCount = Storage::ListStateFolders(
        s_folders, (int)(sizeof(s_folders) / sizeof(s_folders[0])));
    s_selectedTab = 0;
    if (selected[0]) {
        for (int i = 0; i < s_folderCount; ++i) {
            if (strcmp(selected, s_folders[i].name) == 0) {
                s_selectedTab = i + 1;
                break;
            }
        }
    }
    s_foldersLoaded = true;
    s_listLoaded = false;
}

void Initialize()
{
    if (!s_foldersLoaded)
        refreshFolders();
}

static bool needsImmediateRuntimePrep(fopAc_ac_c* linkBeforeLoad)
{
    // The title/file-select scenes have a live Link pointer but no gameplay
    // item/HUD runtime, so a load there must build it immediately (mirroring the
    // game's debug-save load) instead of using the in-game delayed teardown inject.
    // Detect those scenes by, in order of reliability:
    //   - no Link at all, or
    //   - we're before the boot controller-select (manager still at startup mode 5)
    //     -- a rock-solid "title screen" signal that doesn't depend on an actor id,
    //     or
    //   - the title actor is live (covers the file-select scene, after the
    //     controller has been chosen).
    return linkBeforeLoad == nullptr || !dPad_isControllerSelected() ||
           dTitle_isTitleActorLoaded();
}

static void clearRestartStartModeForSaveState()
{
    if (!s_pendingInfo)
        return;

    dSv_restart_c* restart = dSv_getRestartFromInfo(s_pendingInfo);
    static const uint32_t kRestartModeMask = 0xFu;
    const uint32_t oldMode = restart->mLastMode;
    if ((oldMode & kRestartModeMask) == 0)
        return;

    // The low nibble selects Link's scene-entry procedure. Among other values,
    // Zelda.rpx uses 5 for sand/sink recovery and 0xC for a fall/void restart.
    // Those are transition results, not durable state, so replaying a snapshot
    // with that nibble intact repeats the recovery animation on every load.
    // Clear only the start-procedure nibble: bits 4+ retain equipped-item, form,
    // and other metadata. prepareHorseRestartMode() explicitly restores nibble 1
    // afterward for mounted save states.
    restart->mLastMode &= ~kRestartModeMask;
    Logger::Log("[tphd_tools] cleared saved restart start mode: %08X -> %08X",
                (unsigned)oldMode, (unsigned)restart->mLastMode);
}

static void prepareHorseRestartMode()
{
    if (!s_pendingInfo || !s_loadHeader.horseRiding)
        return;

    // checkHorseStart() (FUN_0201a00c) tests `mLastMode & 0xF == 1`, so set the low
    // nibble to 1 in the captured image and keep the upper bits (equipped item and
    // other transition metadata). This patches the value the OV_TEARDOWN memcpy
    // stamps live; dStage_setRestartHorseStart() covers the pre-teardown read.
    static const uint32_t kRestartModeOffset =
        GAME_ADDR_restartMode - GAME_ADDR_gameInfo_info;
    uint32_t* mode = (uint32_t*)(s_pendingInfo + kRestartModeOffset);
    *mode = (*mode & ~0xFu) | 1u;
}

static void preparePositionRestartState()
{
    // Personal/card saves own their return and restart records. Exact-position
    // save states are different: their header is the authoritative room/position
    // and the captured restart tuple may still describe an earlier checkpoint.
    if (!s_pendingInfo || s_normalSaveResume || !s_applyPosNext)
        return;

    dSv_restart_c* restart = dSv_getRestartFromInfo(s_pendingInfo);
    TPHD_BREADCRUMB(
        "[tphd_tools][state-room] patch pending restart: room %d -> %d "
        "pos=(%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) angle=%d -> %d",
        (int)restart->mRoomNo, (int)s_loadHeader.room,
        restart->mRoomPos.x, restart->mRoomPos.y, restart->mRoomPos.z,
        s_loadHeader.pos.x, s_loadHeader.pos.y, s_loadHeader.pos.z,
        (int)restart->mRoomAngleY, (int)s_loadHeader.angle);

    restart->mRoomNo = s_loadHeader.room;
    restart->mRoomPos = s_loadHeader.pos;
    restart->mRoomAngleY = s_loadHeader.angle;
}

struct ActorDeleteContext {
    s16 procId;
    fopAc_ac_c* actors[64];
    uint32_t count;
    uint32_t matched;
};

static fopAc_ac_c* collectActorForDelete(fopAc_ac_c* actor, void* context)
{
    ActorDeleteContext* collect = (ActorDeleteContext*)context;
    if (!actor || fopAcM_GetName(actor) != collect->procId)
        return nullptr;
    // Never delete Link, regardless of proc id: he isn't a name-loaded profile
    // (FPCNM_LINK is only a best-effort id), so guard by pointer identity.
    if (actor == dComIfGp_getPlayer())
        return nullptr;
    ++collect->matched;
    if (collect->count < sizeof(collect->actors) / sizeof(collect->actors[0]))
        collect->actors[collect->count++] = actor;
    return nullptr;
}

static uint32_t deleteActorsByProcId(s16 procId)
{
    if (!actorProcIdValid(procId) || procId == FPCNM_LINK)
        return 0;

    ActorDeleteContext collect = {};
    collect.procId = procId;
    fopAcIt_Judge(collectActorForDelete, &collect);

    uint32_t accepted = 0;
    for (uint32_t i = 0; i < collect.count; ++i) {
        if (fpcM_Delete(collect.actors[i]))
            ++accepted;
    }
    if (collect.matched > collect.count) {
        Logger::LogWarn(
            "[tphd_tools] command delete actor %04X <%s>: "
            "matched %u; capped requests at %u",
            (unsigned)(uint16_t)procId, actorProcName(procId),
            (unsigned)collect.matched, (unsigned)collect.count);
    }
    return accepted;
}

static bool setCurrentAreaFlag(const StateCommand& command)
{
    volatile dSv_memBit_c* area = g_areaTempBit;
    if (!area || command.type != COMMAND_SET_AREA_FLAG)
        return false;

    switch (command.flagType) {
    case AREA_FLAG_CHEST:
        dMem_setBit(area->mTbox, command.flagIndex, command.flagValue != 0);
        return true;
    case AREA_FLAG_SWITCH: {
        typedef void (*SwitchSetter)(void* info, int flag, int roomNo);
        SwitchSetter setter = command.flagValue
            ? (SwitchSetter)0x02aa8714u
            : (SwitchSetter)0x02aa8780u;
        setter((void*)GAME_ADDR_gameInfo_info, command.flagIndex,
               dStage_getRoomNo());
        return true;
    }
    case AREA_FLAG_ITEM:
        dMem_setBit(area->mItem, command.flagIndex, command.flagValue != 0);
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
        fopAc_ac_c* link = dComIfGp_getPlayer();
        const s8 room = link ? link->current.roomNo : dStage_getRoomNo();
        const uint32_t amount = command.amount ? command.amount : 1;
        uint32_t queued = 0;
        for (uint32_t i = 0; i < amount; ++i) {
            if (dActor_spawn(command.procId, command.params, command.subtype,
                             room, &command.position, &command.angle))
                ++queued;
        }
        Logger::Log(
            "[tphd_tools] command frame %u: spawn %04X <%s> x%u "
            "at (%.1f,%.1f,%.1f): %u queued",
            (unsigned)s_execFrame, (unsigned)(uint16_t)command.procId,
            actorProcName(command.procId), (unsigned)amount, command.position.x,
            command.position.y, command.position.z, (unsigned)queued);
        TPHD_BREADCRUMB(
            "[tphd_tools][state-command] frame=%u type=spawn id=%04X amount=%u queued=%u",
            (unsigned)s_execFrame, (unsigned)(uint16_t)command.procId,
            (unsigned)amount, (unsigned)queued);
    } else if (command.type == COMMAND_DELETE_ACTORS) {
        const uint32_t count = deleteActorsByProcId(command.procId);
        Logger::Log(
            "[tphd_tools] command frame %u: delete %04X <%s>: "
            "%u request(s) accepted",
            (unsigned)s_execFrame, (unsigned)(uint16_t)command.procId,
            actorProcName(command.procId), (unsigned)count);
        TPHD_BREADCRUMB(
            "[tphd_tools][state-command] frame=%u type=delete id=%04X count=%u",
            (unsigned)s_execFrame, (unsigned)(uint16_t)command.procId,
            (unsigned)count);
    } else if (command.type == COMMAND_SET_AREA_FLAG) {
        const bool applied = setCurrentAreaFlag(command);
        Logger::Log(
            "[tphd_tools] command frame %u: area flag type=%u index=%u "
            "value=%u: %s",
            (unsigned)s_execFrame, (unsigned)command.flagType,
            (unsigned)command.flagIndex, (unsigned)command.flagValue,
            applied ? "applied" : "failed");
        TPHD_BREADCRUMB(
            "[tphd_tools][state-command] frame=%u type=area-flag category=%u "
            "index=%u value=%u result=%d",
            (unsigned)s_execFrame, (unsigned)command.flagType,
            (unsigned)command.flagIndex, (unsigned)command.flagValue, applied);
    }
}

static void cancelCommandExecution(const char* reason)
{
    if (s_execCommandCount > s_execCommandIndex) {
        Logger::LogWarn(
            "[tphd_tools] canceled %u pending state command(s): %s",
            (unsigned)(s_execCommandCount - s_execCommandIndex), reason);
    }
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

    memcpy(s_execCommands, s_loadCommands,
           s_loadCommandCount * sizeof(StateCommand));
    s_execCommandCount = s_loadCommandCount;
    s_execCommandIndex = 0;
    s_execFrame = 0;
    memcpy(s_execStage, s_ovStage, sizeof(s_execStage));

    // Delete commands run on a floor of kDeleteBaseFrameDelay frames so they never
    // fire while stage actors are still in async creation (which crashes fpcM_Delete).
    // The user's saved delay adds on top. Applied only to this runtime copy, not the
    // stored value, so the editor keeps showing the raw number.
    for (uint32_t i = 0; i < s_execCommandCount; ++i) {
        if (s_execCommands[i].type == COMMAND_DELETE_ACTORS)
            s_execCommands[i].frameDelay += kDeleteBaseFrameDelay;
    }

    // Stable insertion sort: equal-delay commands retain their table order.
    for (uint32_t i = 1; i < s_execCommandCount; ++i) {
        StateCommand command = s_execCommands[i];
        uint32_t j = i;
        while (j > 0 &&
               s_execCommands[j - 1].frameDelay > command.frameDelay) {
            s_execCommands[j] = s_execCommands[j - 1];
            --j;
        }
        s_execCommands[j] = command;
    }

    Logger::Log("[tphd_tools] scheduled %u post-load command(s)",
                (unsigned)s_execCommandCount);
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

    while (s_execCommandIndex < s_execCommandCount &&
           s_execCommands[s_execCommandIndex].frameDelay <= s_execFrame) {
        if (s_execCommands[s_execCommandIndex].enabled)
            executeCommand(s_execCommands[s_execCommandIndex]);
        ++s_execCommandIndex;
    }
    if (s_execCommandIndex >= s_execCommandCount) {
        Logger::Log("[tphd_tools] post-load command list completed");
        cancelCommandExecution("completed");
        return;
    }
    if (s_execFrame != UINT32_MAX)
        ++s_execFrame;
}

static void applyLoadedPosition(fopAc_ac_c* link)
{
    if (s_ovHorseRiding) {
        fopAc_ac_c* horse = dComIfGp_getHorseActor();
        if (horse)
            dHorse_setPosition(horse, &s_ovHorsePos, s_ovHorseAngle, s_ovHorseRoom);
    }
    dStage_setLinkPos(&s_ovPos, s_ovAngle, s_ovRoom);
    dStage_setRestartRoomPos(&s_ovPos, s_ovAngle, s_ovRoom);
}

// ---- per-frame tick ---------------------------------------------------------
// Drop a pending load (timed out or finished) and release its buffer.
static void endLoad()
{
    logRoomState("endLoad", s_ovRoom, s_pendingInfo,
                 s_pendingInfo && !s_pendingSerialized);
    if (s_activeGameSaveTrace) {
        TPHD_BREADCRUMB(
            "[tphd_tools][personal:%u] endLoad: source='%s' phase=%d pendingInfo=%p "
            "serialized=%d normalResume=%d wait=%d",
            (unsigned)s_activeGameSaveTrace, s_gameSaveTraceSource, s_ovPhase,
            (void*)s_pendingInfo, s_pendingSerialized, s_normalSaveResume, s_ovWait);
    }
    if (s_pendingInfo) {
        free(s_pendingInfo);
        s_pendingInfo = nullptr;
    }
    s_ovPhase = OV_IDLE;
    s_ovNeedsRuntimePrep = false;
    s_ovHorseRiding = false;
    s_ovHorseSpawnRequested = false;
    s_ovHorseWait = 0;
    s_ovReadyFrames = 0;
    s_ovCommandsStarted = false;
    s_loadCommandCount = 0;
    s_pendingSerialized = false;
    s_normalSaveResume = false;
    s_activeGameSaveTrace = 0;
    s_gameSaveTraceSource[0] = '\0';
    s_traceWaitEntryLogged = false;
    s_traceTargetReadyLogged = false;
}

// After a normal card-save resume, the scene rebuild can reset the control-pad
// manager back to startup mode (the game expects you to pass through its own
// controller-select screen, which our resume skips), leaving Link with no input
// provider -- looks exactly like a freeze. Re-select the controller for a short
// window after the load so it sticks even if the settle resets it more than once.
static int s_ctrlReselect = 0;

void Tick()
{
    Input::SetSaveStateReloadHotkeyArmed(s_reloadLastHotkey);
    consumeEditResult();

    if (s_ctrlReselect > 0) {
        --s_ctrlReselect;
        if (!dPad_isControllerSelected())
            dPad_ensureControllerSelected(ov::g_settings.controllerPref);
    }

    int loadError = s_loadError;
    if (loadError != 0) {
        s_loadError = 0;
        snprintf(s_status, sizeof(s_status), "%s",
                 loadError == 1 ? "State file no longer exists."
                                : "State load failed or used an unsupported format.");
    }

    if (Input::SaveStateReloadHotkeyFired())
        requestLoad(s_lastLoadedFolder, s_lastLoadedName);

    // A background load finished: kick off a full save-load warp. We do NOT stamp
    // info yet -- if we overwrite it before the warp, the OLD scene tears down
    // reading the new data (wrong stage / water state / flags for the scene it's
    // actually cleaning up), which crashes on state transitions like
    // underwater->land. Instead we inject during the teardown's null-Link window
    // (OV_TEARDOWN), approximating tpgz's dScnPly phase_1 inject: the old scene
    // cleans up with its own data, then the new scene builds from ours.
    if (s_loadReady) {
        cancelCommandExecution("another save state load began");
        logRoomState("loadReady-before-warp", s_loadHeader.room, s_pendingInfo,
                     s_pendingInfo && !s_pendingSerialized);
        if (s_activeGameSaveTrace) {
            char currentStage[9];
            copyPrintableFixed(currentStage, sizeof(currentStage), dStage_getStageName(), 8);
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] Tick consuming loadReady: source='%s' "
                "currentStage='%s' room=%d spawn=%d link=%p padMgr=%p padMode=%u "
                "pendingInfo=%p",
                (unsigned)s_activeGameSaveTrace, s_gameSaveTraceSource, currentStage,
                (int)dStage_getRoomNo(), (int)dStage_getSpawn(),
                (void*)dComIfGp_getPlayer(), dPad_getControlPadMgr(),
                (unsigned)dPad_getControllerMode(), (void*)s_pendingInfo);
            logGameSaveImageState("pending-before-warp", s_pendingInfo,
                                  GAME_DSV_SERIALIZED_SIZE);
        }
        s_loadReady = false;
        s_loadBusy = false;
        // A non-empty ready name identifies a save-state file load. Debug-save
        // in-place reloads and Personal/card saves have their own restart rules.
        if (s_readyLoadName[0])
            clearRestartStartModeForSaveState();
        if (s_readyLoadName[0]) {
            SetLastLoadedStateFolder(s_readyLoadFolder);
            SetLastLoadedStateName(s_readyLoadName);
            s_readyLoadFolder[0] = '\0';
            s_readyLoadName[0] = '\0';
        }
        fopAc_ac_c* linkBeforeLoad = dComIfGp_getPlayer();
        s_ovNeedsRuntimePrep = needsImmediateRuntimePrep(linkBeforeLoad);
        if (s_activeGameSaveTrace) {
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] pre-warp classification: link=%p "
                "controllerSelected=%d titleActor=%d needsRuntimePrep=%d",
                (unsigned)s_activeGameSaveTrace, (void*)linkBeforeLoad,
                dPad_isControllerSelected(), dTitle_isTitleActorLoaded(),
                s_ovNeedsRuntimePrep);
        }
        if (s_ovNeedsRuntimePrep) {
            // Title/file-select load: if we're before the boot controller-select,
            // finalize a controller first (the same call that screen makes) so the
            // warp below doesn't run gameplay against an unselected input provider
            // -- otherwise it crashes. Independent of the save image, so it runs
            // even if s_pendingInfo somehow wasn't set.
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] controller ensure before title/file-select "
                    "warp: mgr=%p mode=%u pref=%d",
                    (unsigned)s_activeGameSaveTrace, dPad_getControlPadMgr(),
                    (unsigned)dPad_getControllerMode(), ov::g_settings.controllerPref);
            }
            dPad_ensureControllerSelected(ov::g_settings.controllerPref);
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] controller ensure returned: mgr=%p mode=%u "
                    "selected=%d",
                    (unsigned)s_activeGameSaveTrace, dPad_getControlPadMgr(),
                    (unsigned)dPad_getControllerMode(), dPad_isControllerSelected());
            }
            s_ovNeedsRuntimePrep = false;
            // if (s_pendingInfo) {
            //     // These loads have no initialized gameplay HUD/item runtime. Mirror
            //     // the game's debug-save path: clear transient save/meter state,
            //     // initialize play item counters, then deserialize the saved image.
            //     dSave_prepareLoadRuntime();
            //     dSave_loadImage(s_pendingInfo);
            // }
        }
        preparePositionRestartState();
        prepareHorseRestartMode();
        logRoomState("loadReady-prepared", s_loadHeader.room, s_pendingInfo,
                     s_pendingInfo && !s_pendingSerialized);
        if (s_normalSaveResume) {
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] calling dStage_loadSavedGame: "
                    "stage='%s' room=%d spawn=%d",
                    (unsigned)s_activeGameSaveTrace, s_loadHeader.stage,
                    (int)s_loadHeader.room, (int)s_loadHeader.spawn);
            }
            dStage_loadSavedGame(s_loadHeader.stage, s_loadHeader.room, s_loadHeader.spawn);
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] dStage_loadSavedGame returned: "
                    "warpPending=%d nextStageEnable=%d",
                    (unsigned)s_activeGameSaveTrace, dStage_warpPending(),
                    *(volatile s8*)(GAME_ADDR_nextStage + DSTAGE_OFF_ENABLE));
            }
        } else {
            dStage_loadStage(s_loadHeader.stage, s_loadHeader.room, s_loadHeader.spawn,
                             s_loadHeader.layer);
        }
        if (s_loadHeader.horseRiding) {
            // dStage_loadStage queues the warp with last-scene-mode 0, so the LIVE
            // restart nibble is 0 right now. Link's recreate reads mLastMode directly
            // from the live block (setStartProcInit), and that read races our
            // OV_TEARDOWN inject of s_pendingInfo. Stamp the horse-start nibble live
            // here, before any recreate can run, so checkHorseStart() is true no matter
            // when Link rebuilds. prepareHorseRestartMode() keeps the same nibble in the
            // pending image so the teardown memcpy doesn't revert it.
            dStage_setRestartHorseStart();
        }
        memcpy(s_ovStage, s_loadHeader.stage, sizeof(s_ovStage));
        s_ovPos        = s_loadHeader.pos;
        s_ovAngle      = s_loadHeader.angle;
        s_ovRoom       = s_loadHeader.room;
        s_ovCamAt      = s_loadHeader.camAt;
        s_ovCamEye     = s_loadHeader.camEye;
        s_ovCamValid   = s_applyPosNext && (s_loadHeader.camValid != 0);
        s_ovHorsePos   = s_loadHeader.horsePos;
        s_ovHorseAngle = s_loadHeader.horseAngle;
        s_ovHorseRoom  = s_loadHeader.horseRoom;
        s_ovHorseRiding = (s_loadHeader.horseRiding != 0);
        s_ovHorseSpawnRequested = false;
        s_ovHorseWait = 0;
        s_ovReadyFrames = 0;
        s_ovApplyPos   = s_applyPosNext;
        s_ovCommandsStarted = false;
        s_ovPhase = s_ovNeedsRuntimePrep ? OV_WAIT : OV_TEARDOWN;
        if (s_ovNeedsRuntimePrep)
            s_ovWait = 300;
        else if (s_normalSaveResume)
            s_ovWait = OV_PERSONAL_TEARDOWN_TIMEOUT;
        else
            s_ovWait = OV_TEARDOWN_TIMEOUT;
        if (s_activeGameSaveTrace) {
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] warp queued: phase=%d wait=%d "
                "applyPos=%d serialized=%d normalResume=%d",
                (unsigned)s_activeGameSaveTrace, s_ovPhase, s_ovWait,
                s_ovApplyPos, s_pendingSerialized, s_normalSaveResume);
        }
    }

    switch (s_ovPhase) {
    case OV_TEARDOWN: {
        // The warp drops the old Link (pMPlayer0 -> null) once the old scene has
        // torn down. Inject the saved block at that moment -- the dying scene
        // never sees it, and the new scene/Link build from it. Save-state loads
        // retain their timeout fallback; Personal saves require the real null-Link
        // barrier and abort instead. Then hand off to OV_WAIT for the new Link.
        bool gone = (dComIfGp_getPlayer() == nullptr);
        bool timedOut = !gone && --s_ovWait <= 0;

        // A Personal save contains serialized item/HUD state. Applying it while
        // the old Link is still alive mutates data underneath the old scene and
        // can crash its next update. Null-Link is therefore a hard barrier for
        // this path: never fall through to the save-state timeout injection.
        if (timedOut && s_normalSaveResume) {
            bool warpPending = dStage_warpPending();
            if (warpPending) {
                // The engine has not consumed the request, so cancel it before
                // releasing the pending image.
                *(volatile s8*)(GAME_ADDR_nextStage + DSTAGE_OFF_ENABLE) = 0;
            }
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] OV_TEARDOWN timed out with Link alive; "
                    "aborting without deserialization: warpPending=%d canceled=%d link=%p",
                    (unsigned)s_activeGameSaveTrace, warpPending, warpPending,
                    (void*)dComIfGp_getPlayer());
            }
            endLoad();
            snprintf(s_status, sizeof(s_status),
                     "Personal save load aborted: scene teardown timed out.");
            break;
        }

        if (gone || timedOut) {
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] OV_TEARDOWN barrier reached: "
                    "linkGone=%d wait=%d pendingInfo=%p serialized=%d",
                    (unsigned)s_activeGameSaveTrace, gone, s_ovWait,
                    (void*)s_pendingInfo, s_pendingSerialized);
            }
            if (s_pendingInfo) {
                if (s_pendingSerialized) {
                    logGameSaveImageState("pre-deserialize", s_pendingInfo,
                                          GAME_DSV_SERIALIZED_SIZE);
                    if (s_activeGameSaveTrace) {
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] calling dSave_prepareLoadRuntime: "
                            "info=%p play=%p",
                            (unsigned)s_activeGameSaveTrace,
                            (void*)GAME_ADDR_gameInfo_info,
                            (void*)GAME_ADDR_gameInfo_play);
                    }
                    dSave_prepareLoadRuntime();
                    if (s_activeGameSaveTrace) {
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] dSave_prepareLoadRuntime returned",
                            (unsigned)s_activeGameSaveTrace);
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] calling dSave_loadImage: image=%p",
                            (unsigned)s_activeGameSaveTrace, (void*)s_pendingInfo);
                    }
                    int deserializeResult = dSave_loadImage(s_pendingInfo);
                    (void)deserializeResult;
                    if (s_activeGameSaveTrace) {
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] dSave_loadImage returned: result=%d",
                            (unsigned)s_activeGameSaveTrace, deserializeResult);
                        logGameSaveImageState(
                            "live-after-deserialize",
                            (const uint8_t*)GAME_ADDR_gameInfo_info, kInfoSize);
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] copying live info back to "
                            "pending buffer: dst=%p src=%p size=0x%X",
                            (unsigned)s_activeGameSaveTrace, (void*)s_pendingInfo,
                            (void*)GAME_ADDR_gameInfo_info, (unsigned)kInfoSize);
                    }
                    memcpy(s_pendingInfo, (const void*)GAME_ADDR_gameInfo_info, kInfoSize);
                    if (s_activeGameSaveTrace) {
                        TPHD_BREADCRUMB(
                            "[tphd_tools][personal:%u] live info copy-back returned",
                            (unsigned)s_activeGameSaveTrace);
                    }
                    s_pendingSerialized = false;
                } else {
                    memcpy((void*)GAME_ADDR_gameInfo_info, s_pendingInfo, kInfoSize);
                }
            } else if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] OV_TEARDOWN has no pendingInfo",
                    (unsigned)s_activeGameSaveTrace);
            }
            logRoomState("teardown-after-inject", s_ovRoom, s_pendingInfo,
                         s_pendingInfo && !s_pendingSerialized);
            s_ovPhase = OV_WAIT;
            s_ovWait  = 300;   // ~20s @30fps before giving up on the recreate
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] phase transition: OV_TEARDOWN -> "
                    "OV_WAIT wait=%d",
                    (unsigned)s_activeGameSaveTrace, s_ovWait);
            }
        }
        break;
    }
    case OV_WAIT: {
        if (s_activeGameSaveTrace && !s_traceWaitEntryLogged) {
            char currentStage[9];
            copyPrintableFixed(currentStage, sizeof(currentStage), dStage_getStageName(), 8);
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] OV_WAIT entered: currentStage='%s' "
                "targetStage='%s' link=%p warpPending=%d wait=%d",
                (unsigned)s_activeGameSaveTrace, currentStage, s_ovStage,
                (void*)dComIfGp_getPlayer(), dStage_warpPending(), s_ovWait);
            s_traceWaitEntryLogged = true;
        }
        if (--s_ovWait <= 0) {     // load never settled -- give up, keep what loaded
            if (s_activeGameSaveTrace) {
                TPHD_BREADCRUMB(
                    "[tphd_tools][personal:%u] OV_WAIT timed out",
                    (unsigned)s_activeGameSaveTrace);
            }
            endLoad();
            break;
        }
        // The warp recreated Link in the target stage. Apply the saved state right
        // away -- no settle delay. The 20-frame "wait until pMPlayer0 stops
        // flickering" guard is gone: it existed because the OLD pre-stamp could
        // change the item record at a bad time and crash the HUD rebuild
        // (FUN_02affcb4) when pMPlayer0 was null. Now the teardown inject already
        // wrote info, so this re-stamp is idempotent (no item-record change -> no
        // HUD rebuild -> no null deref), and we can hand straight to pinning pos.
        fopAc_ac_c* link = dComIfGp_getPlayer();
        bool stageOk = strncmp(dStage_getStageName(), s_ovStage, sizeof(s_ovStage)) == 0;
        bool warpDone = !dStage_warpPending();
        bool targetReady = link && stageOk && warpDone;
        if (s_activeGameSaveTrace && targetReady && !s_traceTargetReadyLogged) {
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] target first ready: link=%p stageOk=%d "
                "warpDone=%d room=%d readyFrames=%d",
                (unsigned)s_activeGameSaveTrace, (void*)link, stageOk, warpDone,
                link ? (int)link->current.roomNo : -1, s_ovReadyFrames);
            s_traceTargetReadyLogged = true;
        }

        // Do not identify a recreated Link by pointer inequality. The actor heap
        // commonly reuses the same address, especially when reloading the same
        // stage/room/spawn, which used to block the position override forever.
        // Every load now follows the same rule: after the teardown barrier above,
        // require a live Link in the target stage for a few consecutive frames.
        if (targetReady) {
            if (s_ovReadyFrames < OV_READY_CONFIRM)
                ++s_ovReadyFrames;
        } else {
            s_ovReadyFrames = 0;
        }
        if (s_ovReadyFrames < OV_READY_CONFIRM)
            break;
        if (s_activeGameSaveTrace) {
            TPHD_BREADCRUMB(
                "[tphd_tools][personal:%u] target readiness confirmed: link=%p "
                "readyFrames=%d horseRiding=%d applyPos=%d normalResume=%d",
                (unsigned)s_activeGameSaveTrace, (void*)link, s_ovReadyFrames,
                s_ovHorseRiding, s_ovApplyPos, s_normalSaveResume);
        }

        if (s_ovHorseRiding) {
            fopAc_ac_c* horse = dComIfGp_getHorseActor();
            if (!horse) {
                // Horse-start mode deliberately keeps Link's create phase waiting
                // for Epona. Most field stages provide her themselves. If this
                // stage does not, queue the normal Horse actor after a short grace
                // period; Link's native create path mounts him when it appears.
                if (!s_ovHorseSpawnRequested && ++s_ovHorseWait >= OV_HORSE_SPAWN_GRACE) {
                    const cXyz& pos = s_ovApplyPos ? s_ovHorsePos : link->current.pos;
                    s16 angleY = s_ovApplyPos ? s_ovHorseAngle : link->shape_angle.y;
                    s8 room = s_ovApplyPos ? s_ovHorseRoom : link->current.roomNo;
                    csXyz angle = { 0, angleY, 0 };
                    if (dActor_spawn(FPCNM_HORSE, 0, -1, room, &pos, &angle)) {
                        s_ovHorseSpawnRequested = true;
                        Logger::Log("[tphd_tools] state load: spawned Epona in target stage");
                    } else {
                        s_ovHorseWait = OV_HORSE_SPAWN_GRACE / 2;
                    }
                }
                break;
            }
            if (s_ovHorseWait > 0 && !s_ovHorseSpawnRequested) {
                Logger::Log("[tphd_tools] state load: using stage-provided Epona");
                s_ovHorseWait = 0;
            }
            // The pointer is published near the end of Epona's create. Give
            // Link's retried create pass time to run initForceRideHorse().
            if (!dPlayer_isHorseRiding(link))
                break;
        }
        if (targetReady) {
            if (s_ovHorseRiding) {
                fopAc_ac_c* horse = dComIfGp_getHorseActor();
                Logger::Log("[tphd_tools] state load: Link mounted on Epona; "
                            "target (%.1f,%.1f,%.1f) room %d, Epona now (%.1f,%.1f,%.1f)",
                            s_ovHorsePos.x, s_ovHorsePos.y, s_ovHorsePos.z, (int)s_ovHorseRoom,
                            horse ? horse->current.pos.x : 0.0f,
                            horse ? horse->current.pos.y : 0.0f,
                            horse ? horse->current.pos.z : 0.0f);
            }
            // The full-block re-stamp is a SAVE-STATE mechanism: a save-state captured
            // the entire live block, so re-applying it after the scene rebuilt restores
            // exactly that state. A normal card-save resume is different: the teardown
            // deserialize already loaded the save, and the engine rebuilds the live
            // working memory (mMemory) from mSave[stage] -- via its own getSave -- as
            // the scene builds. Re-stamping the whole 0x13D8 block here would undo that
            // (e.g. zero the current-stage key, which lives in mMemory) AND overwrite
            // live scene state (mZone/mRestart) mid-rebuild, wedging the recreation. So
            // only the save-state path re-stamps; normal-resume defers to the engine.
            if (s_pendingInfo && !s_normalSaveResume)
                memcpy((void*)GAME_ADDR_gameInfo_info, s_pendingInfo, kInfoSize);
            if (s_ovNeedsRuntimePrep)
                dComIfGp_itemDataInit((void*)GAME_ADDR_gameInfo_play);
            if (s_normalSaveResume) {
                // Re-select the controller for ~5s after the resume so it survives any
                // pad-manager reset the scene settle does (title-screen loads especially).
                if (s_activeGameSaveTrace) {
                    TPHD_BREADCRUMB(
                        "[tphd_tools][personal:%u] final controller ensure: mgr=%p "
                        "modeBefore=%u pref=%d",
                        (unsigned)s_activeGameSaveTrace, dPad_getControlPadMgr(),
                        (unsigned)dPad_getControllerMode(), ov::g_settings.controllerPref);
                }
                dPad_ensureControllerSelected(ov::g_settings.controllerPref);
                s_ctrlReselect = 150;
                if (s_activeGameSaveTrace) {
                    TPHD_BREADCRUMB(
                        "[tphd_tools][personal:%u] final controller ensure returned: "
                        "modeAfter=%u selected=%d",
                        (unsigned)s_activeGameSaveTrace,
                        (unsigned)dPad_getControllerMode(), dPad_isControllerSelected());
                }
            }
            if (!s_ovApplyPos) {   // spawn-point load (debug saves): no teleport
                logRoomState("target-ready-no-position-override", s_ovRoom, s_pendingInfo,
                             s_pendingInfo && !s_pendingSerialized);
                if (s_activeGameSaveTrace) {
                    logGameSaveImageState(
                        "live-load-complete",
                        (const uint8_t*)GAME_ADDR_gameInfo_info, kInfoSize);
                    TPHD_BREADCRUMB(
                        "[tphd_tools][personal:%u] Personal load completed at spawn",
                        (unsigned)s_activeGameSaveTrace);
                }
                startCommandExecution();
                endLoad();
                snprintf(s_status, sizeof(s_status), "Loaded.");
            } else {
                applyLoadedPosition(link);
                logRoomState("target-ready-position-applied", s_ovRoom, s_pendingInfo,
                             s_pendingInfo && !s_pendingSerialized);
                startCommandExecution();
                s_ovPhase  = OV_APPLY;
                s_ovApply  = s_ovHorseRiding ? OV_HORSE_HOLD_FRAMES : OV_HOLD_TIMEOUT;
                s_ovGround = OV_GROUND_CONFIRM;
            }
        }
        break;
    }
    case OV_APPLY: {
        // Pin exact-position loads until collision settles.
        fopAc_ac_c* link = dComIfGp_getPlayer();
        if (!link || dStage_warpPending()) {
            endLoad();
            snprintf(s_status, sizeof(s_status), "Loaded.");
            break;
        }
        if (s_ovApplyPos) {
            applyLoadedPosition(link);
            *dComIfGp_getOxygen() = 600;   // refill air so a deep-water load doesn't drown
            // Pin the camera at the saved view each frame too, so it holds the saved
            // framing through the settle instead of reframing Link from a default.
            if (s_ovCamValid) {
                CameraXform* cam = dCam_getXform();
                if (cam) {
                    cam->at  = s_ovCamAt;
                    cam->eye = s_ovCamEye;
                }
            }
        }
        bool positionDone = false;
        if (!s_ovApplyPos) {
            positionDone = true;
        } else if (s_ovHorseRiding) {
            // Epona's create and Link's force-ride initialization already wait
            // for their collision state. Do not test Link's foot contact here:
            // mounted Link is intentionally above the floor and would otherwise
            // remain frozen until the long airborne timeout.
            positionDone = (--s_ovApply <= 0);
        } else {
            if (dPlayer_onGround(link)) {
                if (--s_ovGround <= 0)        // grounded a few frames running -> settled
                    positionDone = true;
            } else {
                s_ovGround = OV_GROUND_CONFIRM;
            }
            if (--s_ovApply <= 0)             // airborne save / floor never streamed
                positionDone = true;
        }
        if (positionDone) {
            endLoad();
            snprintf(s_status, sizeof(s_status), "Loaded.");
        }
        break;
    }
    default:
        break;
    }

    tickCommandExecution();
}

// ---- window -----------------------------------------------------------------
static const char* commandTypeName(uint16_t type)
{
    switch (type) {
    case COMMAND_SPAWN_ACTOR:    return "Spawn actor";
    case COMMAND_DELETE_ACTORS:  return "Delete actor(s)";
    case COMMAND_SET_AREA_FLAG:  return "Set current-area flag";
    default:                     return "Unknown";
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
        snprintf(out, outSize, "%s%04X %s at %.1f, %.1f, %.1f",
                 count, (unsigned)(uint16_t)command.procId,
                 actorDisplayName(command.procId),
                 command.position.x, command.position.y, command.position.z);
    } else if (command.type == COMMAND_DELETE_ACTORS) {
        snprintf(out, outSize, "all %04X %s",
                 (unsigned)(uint16_t)command.procId,
                 actorDisplayName(command.procId));
    } else if (command.flagType == AREA_FLAG_KEY_COUNT) {
        snprintf(out, outSize, "Key count = %u",
                 (unsigned)command.flagIndex);
    } else {
        snprintf(out, outSize, "%s %u = %s",
                 areaFlagTypeName(command.flagType),
                 (unsigned)command.flagIndex,
                 command.flagValue ? "On" : "Off");
    }
}

static void setEditorForm(const StateCommand& command, int index)
{
    s_editorForm = command;
    s_editorFormIndex = index;
    s_editorActorQuery[0] = '\0';   // the query is a filter only; selection is procId
}

// Actor picker. The text box is a FILTER ONLY -- it narrows the list and never
// assigns; selection happens by clicking an entry. Returns the selected proc id
// (-1 = none). The list shows the friendly name, falling back to the proc name.
static s16 drawActorSelector()
{
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##actorfilter", "filter: Keese, e_ba, 1EB...",
                             s_editorActorQuery, sizeof(s_editorActorQuery));
    if (s_editorActorQuery[0]) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))   // clear; only shown when there's text
            s_editorActorQuery[0] = '\0';
    }
    ImGui::SameLine();
    ImGui::TextDisabled("filter");

    s16 sel = s_editorForm.procId;
    if (actorProcIdValid(sel)) {
        const char* fr = actorFriendlyName(sel);
        ImGui::TextDisabled("Selected: 0x%04X <%s>%s%s",
                            (unsigned)(uint16_t)sel, actorProcName(sel),
                            fr[0] ? " " : "", fr);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "No actor selected.");
    }

    const char* preview = actorProcIdValid(sel) ? actorDisplayName(sel) : "Select...";
    if (ImGui::BeginCombo("Actor", preview)) {
        const uint32_t count = (uint32_t)(procs_bin_size / kProcRecordSize);
        for (uint32_t i = 0; i < count; ++i) {
            if (!actorProcIdValid((s16)i))
                continue;
            const char* name = actorProcName((s16)i);
            const char* friendly = actorFriendlyName((s16)i);
            if (!containsIgnoreCase(name, s_editorActorQuery) &&
                !containsIgnoreCase(friendly, s_editorActorQuery))
                continue;
            char label[96];
            if (friendly[0])
                snprintf(label, sizeof(label), "%04X  %s  (%s)", (unsigned)i, friendly, name);
            else
                snprintf(label, sizeof(label), "%04X  %s", (unsigned)i, name);
            bool selected = (sel == (s16)i);
            if (ImGui::Selectable(label, selected)) {
                s_editorForm.procId = (s16)i;
                sel = (s16)i;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return s_editorForm.procId;
}

static void drawCommandForm()
{
    static const char* kTypes[] = {
        "Spawn actor", "Delete actor(s)", "Set current-area flag"
    };
    int typeIndex = (int)s_editorForm.type - 1;
    if (typeIndex < 0 || typeIndex >= 3)
        typeIndex = 0;
    if (ImGui::Combo("Action", &typeIndex, kTypes, 3)) {
        const uint32_t delay = s_editorForm.frameDelay;
        const s16 keepProc = s_editorForm.procId;   // persist across Spawn<->Delete
        const u16 keepAmount = s_editorForm.amount;
        const uint16_t newType = (uint16_t)(typeIndex + 1);
        resetEditorForm(newType);
        s_editorForm.frameDelay = delay;
        if (newType == COMMAND_SPAWN_ACTOR || newType == COMMAND_DELETE_ACTORS) {
            s_editorForm.procId = keepProc;
            s_editorForm.amount = keepAmount;
        }
    }

    const uint32_t one = 1;
    const uint32_t thirty = 30;
    ImGui::InputScalar("Frame delay", ImGuiDataType_U32,
                       &s_editorForm.frameDelay, &one, &thirty);

    bool actorValid = true;
    if (s_editorForm.type == COMMAND_SPAWN_ACTOR) {
        actorValid = drawActorSelector() >= 0;
        ImGui::InputScalar("Parameters", ImGuiDataType_U32,
                           &s_editorForm.params, nullptr, nullptr,
                           "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        int subtype = s_editorForm.subtype;
        if (ImGui::InputInt("Subtype", &subtype))
            s_editorForm.subtype = (s8)(subtype < -128 ? -128 :
                                        subtype > 127 ? 127 : subtype);
        int amount = s_editorForm.amount ? s_editorForm.amount : 1;
        if (ImGui::InputInt("Amount", &amount))
            s_editorForm.amount = (u16)(amount < 1 ? 1 :
                                        amount > kMaxSpawnAmount ? kMaxSpawnAmount
                                                                 : amount);
        ImGui::TextDisabled("Spawns this many copies at the same spot (max %u).",
                            (unsigned)kMaxSpawnAmount);
        ImGui::InputFloat3("Position", &s_editorForm.position.x, "%.3f");
        int yaw = s_editorForm.angle.y;
        if (ImGui::InputInt("Yaw", &yaw))
            s_editorForm.angle.y =
                (s16)(yaw < -32768 ? -32768 : yaw > 32767 ? 32767 : yaw);
        if (ImGui::Button("Use Link position")) {
            fopAc_ac_c* link = dComIfGp_getPlayer();
            if (link) {
                s_editorForm.position = link->current.pos;
                s_editorForm.angle.y = link->shape_angle.y;
            }
        }
    } else if (s_editorForm.type == COMMAND_DELETE_ACTORS) {
        actorValid = drawActorSelector() >= 0;
        if (s_editorForm.procId == FPCNM_LINK) {
            actorValid = false;
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "Link cannot be deleted.");
        }
        ImGui::TextDisabled("All live actors with this process ID are requested for deletion.");
    } else {
        static const char* kFlagTypes[] = {
            "Chest", "Switch", "Item", "Dungeon item", "Key count"
        };
        int flagType = s_editorForm.flagType;
        if (flagType < 0 || flagType >= 5)
            flagType = AREA_FLAG_SWITCH;
        if (ImGui::Combo("Flag category", &flagType, kFlagTypes, 5))
            s_editorForm.flagType = (uint16_t)flagType;

        int value = s_editorForm.flagIndex;
        if (s_editorForm.flagType == AREA_FLAG_KEY_COUNT) {
            if (ImGui::InputInt("Key count", &value))
                s_editorForm.flagIndex =
                    (u16)(value < 0 ? 0 : value > 255 ? 255 : value);
        } else {
            int maximum = s_editorForm.flagType == AREA_FLAG_CHEST ? 63 :
                          s_editorForm.flagType == AREA_FLAG_SWITCH ? 127 :
                          s_editorForm.flagType == AREA_FLAG_ITEM ? 31 : 7;
            if (ImGui::InputInt("Flag index", &value))
                s_editorForm.flagIndex =
                    (u16)(value < 0 ? 0 : value > maximum ? maximum : value);
            bool enabled = s_editorForm.flagValue != 0;
            if (ImGui::Checkbox("Set flag", &enabled))
                s_editorForm.flagValue = enabled ? 1 : 0;
            ImGui::TextDisabled("Valid range: 0-%d", maximum);
        }
    }

    const bool valid = actorValid && commandSemanticsValid(s_editorForm);
    if (!valid)
        ImGui::BeginDisabled();
    const char* buttonLabel =
        s_editorFormIndex >= 0 ? "Update command" : "Add command";
    if (ImGui::Button(buttonLabel)) {
        if (s_editorFormIndex >= 0 &&
            (uint32_t)s_editorFormIndex < s_editorCommandCount) {
            s_editorCommands[s_editorFormIndex] = s_editorForm;
            snprintf(s_editorStatus, sizeof(s_editorStatus),
                     "Command updated.");
        } else if (s_editorCommandCount < kMaxCommands) {
            s_editorCommands[s_editorCommandCount++] = s_editorForm;
            snprintf(s_editorStatus, sizeof(s_editorStatus),
                     "Command added.");
        } else {
            snprintf(s_editorStatus, sizeof(s_editorStatus),
                     "The command list is full.");
        }
        resetEditorForm(s_editorForm.type);
    }
    if (!valid)
        ImGui::EndDisabled();
    if (s_editorFormIndex >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel edit"))
            resetEditorForm(COMMAND_SPAWN_ACTOR);
    }
}

static void drawCommandEditor(bool menuActive)
{
    if (!s_editorOpen)
        return;
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save State Commands", &s_editorOpen, flags)) {
        ImGui::Text("State: %s%s%s",
                    s_editorFolder[0] ? s_editorFolder : "Main",
                    "/", s_editorName[0] ? s_editorName : "(loading)");
        if (s_editorStatus[0])
            ImGui::TextDisabled("%s", s_editorStatus);
        ImGui::Separator();

        if (s_editBusy) {
            ImGui::TextDisabled("Loading state command data ...");
        } else if (!s_editorBaseImage) {
            ImGui::TextDisabled("No editable command data is available.");
            if (ImGui::Button("Close"))
                s_editorOpen = false;
        } else {
            drawCommandForm();
            ImGui::Separator();

            ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("##state_commands", 5, tableFlags,
                                  ImVec2(0.0f, 230.0f))) {
                ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Delay", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 145.0f);
                ImGui::TableSetupColumn("Details");
                ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 135.0f);
                ImGui::TableHeadersRow();
                for (uint32_t i = 0; i < s_editorCommandCount; ++i) {
                    ImGui::PushID((int)i);
                    ImGui::TableNextRow();
                    const bool on = s_editorCommands[i].enabled != 0;
                    ImGui::TableSetColumnIndex(0);
                    bool toggled = on;
                    // Toggling persists immediately so disabling a command to skip it
                    // (instead of deleting it) survives without a separate save.
                    if (ImGui::Checkbox("##enabled", &toggled)) {
                        s_editorCommands[i].enabled = toggled ? 1 : 0;
                        persistEditorCommands(toggled ? "Command enabled (saved)."
                                                      : "Command disabled (saved).");
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (!on) ImGui::BeginDisabled();
                    ImGui::Text("%u", (unsigned)s_editorCommands[i].frameDelay);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(commandTypeName(s_editorCommands[i].type));
                    ImGui::TableSetColumnIndex(3);
                    char summary[192];
                    commandSummary(s_editorCommands[i], summary, sizeof(summary));
                    ImGui::TextUnformatted(summary);
                    if (!on) ImGui::EndDisabled();
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::SmallButton("Edit"))
                        setEditorForm(s_editorCommands[i], (int)i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Del")) {
                        for (uint32_t j = i + 1; j < s_editorCommandCount; ++j)
                            s_editorCommands[j - 1] = s_editorCommands[j];
                        --s_editorCommandCount;
                        if (s_editorFormIndex == (int)i)
                            resetEditorForm(COMMAND_SPAWN_ACTOR);
                        else if (s_editorFormIndex > (int)i)
                            --s_editorFormIndex;
                        --i;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (ImGui::Button("Save changes"))
                saveEditorCommands();
            ImGui::SameLine();
            if (ImGui::Button("Close"))
                s_editorOpen = false;
        }
    }
    ImGui::End();

    if (!s_editorOpen && !s_editBusy && s_editorBaseImage) {
        free(s_editorBaseImage);
        s_editorBaseImage = nullptr;
        s_editorCommandCount = 0;
    }
}

void DrawWindow(bool menuActive)
{
    if (!s_enabled)
        return;
    if (!s_foldersLoaded)
        refreshFolders();
    if (s_refreshIn > 0 && --s_refreshIn == 0)
        s_listLoaded = false;

    // No NoSavedSettings: ImGui tracks this window's pos/size, persisted via the
    // config file's imgui-ini blob (see config.cpp).
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuActive)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(370.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Loader", &s_enabled, flags)) {
        ImGui::TextDisabled("Snapshots your save + position. Loading warps you back.");
        ImGui::Checkbox("Override saved position", &s_overridePosition);
        if (!s_overridePosition)
            ImGui::TextDisabled("Uses the saved stage, room, layer, and spawn point.");
        ImGui::Checkbox("Reload last state hotkey (ZL + ZR + Start)", &s_reloadLastHotkey);
        if (s_lastLoadedName[0]) {
            ImGui::TextDisabled("Last loaded: %s%s%s",
                                s_lastLoadedFolder[0] ? s_lastLoadedFolder : "Main",
                                "/", s_lastLoadedName);
        } else if (s_reloadLastHotkey) {
            ImGui::TextDisabled("Load a state once before using the hotkey.");
        }

        if (ImGui::BeginTabBar("##state_folders")) {
            if (ImGui::BeginTabItem("Main")) {
                if (s_selectedTab != 0) {
                    s_selectedTab = 0;
                    s_listLoaded = false;
                }
                ImGui::EndTabItem();
            }
            for (int i = 0; i < s_folderCount; ++i) {
                char label[64];
                strncpy(label, s_folders[i].name, sizeof(label) - 1);
                label[sizeof(label) - 1] = '\0';
                for (char* p = label; *p; ++p) {
                    if (*p == '#')
                        *p = '_';
                }
                if (label[0] >= 'a' && label[0] <= 'z')
                    label[0] = (char)(label[0] - 'a' + 'A');
                char tabId[96];
                snprintf(tabId, sizeof(tabId), "%s##state_folder_%d", label, i);
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
        if (!s_listLoaded)
            refreshList();

        ImGui::SetNextItemWidth(400.0f);
        ImGui::InputTextWithHint("##name", "state name (optional)", s_nameBuf, sizeof(s_nameBuf));
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
        ImGui::BeginChild("##states", ImVec2(0, 0), true);
        if (s_fileCount == 0) {
            ImGui::TextDisabled("No saved states yet.");
        }
        for (int i = 0; i < s_fileCount; ++i) {
            ImGui::PushID(i);
            if (ImGui::Button("Load"))
                requestLoad(selectedFolder(), s_files[i].name);
            ImGui::SameLine();
            if (ImGui::Button("Edit"))
                requestEditLoad(selectedFolder(), s_files[i].name);
            ImGui::SameLine();
            if (ImGui::Button("Del")) {
                s_confirmDelete   = i;
                s_openDeletePopup = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(s_files[i].label);
            ImGui::PopID();
        }
        ImGui::EndChild();

        // Delete confirmation (opened once on the Del click).
        if (s_openDeletePopup) {
            ImGui::OpenPopup("Delete state?");
            s_openDeletePopup = false;
        }
        if (s_confirmDelete >= 0 && s_confirmDelete < s_fileCount &&
            ImGui::BeginPopupModal("Delete state?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete \"%s\"?", s_files[s_confirmDelete].label);
            ImGui::Spacing();
            if (ImGui::Button("Delete")) {
                requestDelete(selectedFolder(), s_files[s_confirmDelete].name);
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
    }
    ImGui::End();
    drawCommandEditor(menuActive);
}

} // namespace SaveState
} // namespace Tools
