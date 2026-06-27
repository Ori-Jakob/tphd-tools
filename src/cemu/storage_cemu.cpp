// storage_cemu.cpp -- SD-card backend for the Cemu/RPL build.
//

#include "storage.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>          // memalign (aligned FS write bounce buffer)

#include <coreinit/debug.h>
#include <coreinit/filesystem.h>
#include <coreinit/atomic.h>     // OSCompareAndSwapAtomic (one-time mount election)
#include <coreinit/thread.h>     // OSSleepTicks (mount-election wait)
#include <coreinit/time.h>       // OSMillisecondsToTicks

namespace Storage {

// Relative paths (under the SD mount), identical to the Aroma backend.
static const char* kRootDir    = "tphd_tools";
static const char* kConfigPath = "tphd_tools/config.json";
static const char* kLogPath    = "tphd_tools/log.txt";
static const char* kOldLogPath = "tphd_tools/log.txt.old";
static const char* kStateDir   = "tphd_tools/savestates";
static const char* kGameSaveDir = "tphd_tools/saves";
static const char* kSplitHistoryDir = "tphd_tools/split_times";
static const char* kSplitGoldDir = "tphd_tools/split_golds";
static const char* kSplitDir = "tphd_tools/splits";


// SD mount (one time, thread-safe). TPHD's title lacks the cos.xml SDCARD_MOUNT
// capability, so Cemu's FSGetMountSource(SD)/FSMount path is permission-gated and
// fails with -2. But FSBindMount("/dev/sdcard01" -> "/vol/external01") performs the
// host-FS mount with NO permission check (see Cemu coreinit_FS.cpp: FSBindMount ->
// mountSDCard()), which is the same SD that Aroma reaches over stdio. We bind it
// once and then use plain FS paths under /vol/external01.
//
// ensureMount() is hit from multiple threads (present thread via Config/SaveState,
// the logger worker via PrepareLog), so a single static FSClient must be added
// exactly once -- a second FSAddClient on the same client is a fatal FS error.
// We elect one initializer with an atomic CAS and have the rest wait on the result.
static FSClient   s_mountClient;
static FSCmdBlock s_mountCmd;
static char       s_sdRoot[] = "/vol/external01";  // Cemu's fixed SD mount point

enum { MOUNT_UNTRIED = 0, MOUNT_BUSY = 1, MOUNT_OK = 2, MOUNT_FAILED = 3 };
static volatile uint32_t s_mountState = MOUNT_UNTRIED;

static bool doMount()
{
    if (FSAddClient(&s_mountClient, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) {
        OSReport("[tphd_tools][storage] SD mount: FSAddClient failed\n");
        return false;
    }
    FSInitCmdBlock(&s_mountCmd);

    // Bypass the SDCARD_MOUNT permission check via the bind-mount path. Cemu returns
    // an error if it was already mounted; in that case /vol/external01 is still
    // usable, so we probe it before giving up.
    FSStatus r = FSBindMount(&s_mountClient, &s_mountCmd,
                             (char*)"/dev/sdcard01", s_sdRoot, FS_ERROR_FLAG_ALL);
    if (r == FS_STATUS_OK) {
        OSReport("[tphd_tools][storage] SD bind-mounted at '%s'\n", s_sdRoot);
        return true;
    }

    // Already mounted (or bind unsupported): confirm the path is reachable.
    FSDirectoryHandle dh;
    if (FSOpenDir(&s_mountClient, &s_mountCmd, s_sdRoot, &dh, FS_ERROR_FLAG_ALL) ==
        FS_STATUS_OK) {
        FSCloseDir(&s_mountClient, &s_mountCmd, dh, FS_ERROR_FLAG_ALL);
        OSReport("[tphd_tools][storage] SD already mounted at '%s' (FSBindMount=%d)\n",
                 s_sdRoot, (int)r);
        return true;
    }

    OSReport("[tphd_tools][storage] SD mount failed: FSBindMount=%d (no SD access)\n", (int)r);
    FSDelClient(&s_mountClient, FS_ERROR_FLAG_ALL);
    return false;
}

static bool ensureMount()
{
    for (;;) {
        uint32_t st = s_mountState;
        if (st == MOUNT_OK)
            return true;
        if (st == MOUNT_FAILED)
            return false;   // tried once, don't spam-retry every frame
        if (st == MOUNT_BUSY) {
            // Another thread is doing the blocking FSBindMount. Sleep a real
            // interval rather than OSYieldThread(): yield only round-robins
            // EQUAL-priority threads, so if this waiter is higher priority and on
            // the same core as the mounter, a busy-yield starves it and the mount
            // never completes (intermittent splash-screen hang). A short sleep
            // always lets the mounter -- and the FS I/O threads -- run.
            OSSleepTicks(OSMillisecondsToTicks(1));
            continue;
        }
        // st == MOUNT_UNTRIED: try to become the single initializer.
        if (OSCompareAndSwapAtomic(&s_mountState, MOUNT_UNTRIED, MOUNT_BUSY)) {
            bool ok = doMount();
            s_mountState = ok ? MOUNT_OK : MOUNT_FAILED;
            return ok;
        }
        // Lost the race; loop to observe the winner's result.
    }
}

// Build an absolute SD path "<sdRoot>/<relative>" from a relative tphd_tools path.
static bool makePath(const char* relative, char* out, int outSize)
{
    if (!relative || !out || outSize <= 0)
        return false;
    int n = snprintf(out, outSize, "%s/%s", s_sdRoot, relative);
    return n > 0 && n < outSize;
}

// Small FS helpers
static bool beginFs(FSClient* client, FSCmdBlock* cmd)
{
    if (FSAddClient(client, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return false;
    FSInitCmdBlock(cmd);
    return true;
}

static void endFs(FSClient* client)
{
    FSDelClient(client, FS_ERROR_FLAG_ALL);
}

// FS DMA wants a 64-byte-aligned source buffer; malloc'd config/log text usually
// isn't. Copy through an aligned bounce so the buffer pointer is never the cause
// of a write failure. Returns FSWriteFile's status.
static FSStatus writeAligned(FSClient* client, FSCmdBlock* cmd, FSFileHandle h,
                             const void* src, uint32_t size)
{
    uint32_t cap = (size + 63u) & ~63u;
    uint8_t* bounce = (uint8_t*)memalign(64, cap);
    if (!bounce)
        return (FSStatus)-1;
    memcpy(bounce, src, size);
    FSStatus n = FSWriteFile(client, cmd, bounce, 1, size, h, 0, FS_ERROR_FLAG_ALL);
    free(bounce);
    return n;
}

// Create each component of an absolute SD directory path (mkdir -p). Treats
// "already exists" as success; any other error stops and returns false.
static bool makeDirs(FSClient* client, FSCmdBlock* cmd, const char* absDir)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s", absDir);
    if (n <= 0 || n >= (int)sizeof(buf))
        return false;

    // Skip past the mount root ("/vol/external01") so we only create our own dirs.
    char* p = buf + strlen(s_sdRoot);
    if (*p == '/')
        ++p;

    for (; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        FSStatus r = FSMakeDir(client, cmd, buf, FS_ERROR_FLAG_ALL);
        if (r != FS_STATUS_OK && r != FS_STATUS_EXISTS) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    FSStatus r = FSMakeDir(client, cmd, buf, FS_ERROR_FLAG_ALL);
    return r == FS_STATUS_OK || r == FS_STATUS_EXISTS;
}

// Ensure a relative tphd_tools directory exists under the SD mount.
static bool ensureRelDir(FSClient* client, FSCmdBlock* cmd, const char* relativeDir)
{
    char abs[256];
    if (!makePath(relativeDir, abs, sizeof(abs)))
        return false;
    return makeDirs(client, cmd, abs);
}

static bool validStateFolder(const char* folder)
{
    if (!folder || !folder[0])
        return true;
    if (strcmp(folder, ".") == 0 || strcmp(folder, "..") == 0)
        return false;
    for (const char* p = folder; *p; ++p) {
        if (*p == '/' || *p == '\\')
            return false;
    }
    return true;
}

static bool buildStateDir(const char* folder, char* out, int outSize)
{
    if (!out || outSize <= 0 || !validStateFolder(folder))
        return false;
    int n = folder && folder[0]
                ? snprintf(out, outSize, "%s/%s", kStateDir, folder)
                : snprintf(out, outSize, "%s", kStateDir);
    return n > 0 && n < outSize;
}

static bool buildStatePath(const char* folder, const char* name, char* out, int outSize)
{
    if (!name || !name[0])
        return false;
    char dir[128];
    if (!buildStateDir(folder, dir, sizeof(dir)))
        return false;
    char relative[160];
    int n = snprintf(relative, sizeof(relative), "%s/%s.bin", dir, name);
    if (n <= 0 || n >= (int)sizeof(relative))
        return false;
    return makePath(relative, out, outSize);
}

static bool endsWithBin(const char* n)
{
    size_t l = strlen(n);
    return l > 4 && strcmp(n + l - 4, ".bin") == 0;
}

static bool endsWithDat(const char* n)
{
    size_t l = strlen(n);
    return l > 4 && strcmp(n + l - 4, ".dat") == 0;
}

static bool endsWithJson(const char* n)
{
    size_t l = strlen(n);
    return l > 5 && strcmp(n + l - 5, ".json") == 0 &&
           (l < 13 || strcmp(n + l - 13, ".history.json") != 0);
}

static bool validSplitPath(const char* path)
{
    if (!path || !path[0] || strstr(path, "..") || path[0] == '/' || path[0] == '\\')
        return false;
    size_t n = strlen(kSplitDir);
    if (strlen(path) <= n)
        return false;
    return strncmp(path, kSplitDir, n) == 0 && path[n] == '/' && path[n + 1] != '\0';
}

static bool validHistoryKey(const char* key)
{
    if (!key || !key[0])
        return false;
    for (const char* p = key; *p; ++p)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ))
            return false;
    return true;
}

static ReadResult loadTextPath(const char* relativePath, char** outText, uint32_t maxBytes)
{
    if (!outText || !relativePath || maxBytes < 2)
        return READ_ERROR;
    *outText = nullptr;
    if (!ensureMount())
        return READ_RETRY;

    char path[256];
    if (!makePath(relativePath, path, sizeof(path)))
        return READ_ERROR;
    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return READ_RETRY;
    FSFileHandle h;
    FSStatus r = FSOpenFile(&client, &cmd, path, "r", &h, FS_ERROR_FLAG_ALL);
    if (r == FS_STATUS_NOT_FOUND) {
        endFs(&client);
        return READ_MISSING;
    }
    if (r != FS_STATUS_OK) {
        endFs(&client);
        return READ_ERROR;
    }
    FSStat st;
    ReadResult result = READ_ERROR;
    if (FSGetStatFile(&client, &cmd, h, &st, FS_ERROR_FLAG_ALL) == FS_STATUS_OK &&
        st.size > 0 && st.size < maxBytes) {
        char* text = (char*)malloc(st.size + 1);
        if (text) {
            FSStatus n = FSReadFile(&client, &cmd, (uint8_t*)text, 1, st.size, h, 0,
                                    FS_ERROR_FLAG_ALL);
            if (n >= 0) {
                text[(uint32_t)n] = '\0';
                *outText = text;
                result = READ_OK;
            } else {
                free(text);
            }
        }
    }
    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return result;
}

static bool saveTextPath(const char* relativeDir, const char* relativePath, const char* text)
{
    if (!relativeDir || !relativePath || !text || !ensureMount())
        return false;
    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;
    if (!ensureRelDir(&client, &cmd, relativeDir)) {
        endFs(&client);
        return false;
    }
    char path[256];
    if (!makePath(relativePath, path, sizeof(path))) {
        endFs(&client);
        return false;
    }
    FSFileHandle h;
    bool ok = false;
    if (FSOpenFile(&client, &cmd, path, "w", &h, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        uint32_t len = (uint32_t)strlen(text);
        FSStatus n = writeAligned(&client, &cmd, h, text, len);
        FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
        ok = n == (FSStatus)len;
    }
    endFs(&client);
    return ok;
}

static bool buildGameSavePath(const char* name, char* out, int outSize)
{
    if (!name || !name[0])
        return false;
    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\')
            return false;
    }
    char relative[160];
    int n = snprintf(relative, sizeof(relative), "%s/%s.dat", kGameSaveDir, name);
    return n > 0 && n < (int)sizeof(relative) && makePath(relative, out, outSize);
}


// Storage interface
bool EnsureReady()
{
    if (!ensureMount())
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;
    bool ok = ensureRelDir(&client, &cmd, kRootDir) &&
              ensureRelDir(&client, &cmd, kStateDir) &&
              ensureRelDir(&client, &cmd, kGameSaveDir) &&
              ensureRelDir(&client, &cmd, kSplitHistoryDir) &&
              ensureRelDir(&client, &cmd, kSplitGoldDir) &&
              ensureRelDir(&client, &cmd, kSplitDir);
    endFs(&client);
    return ok;
}

ReadResult LoadConfig(char** outText, uint32_t maxBytes)
{
    if (!outText)
        return READ_ERROR;
    *outText = nullptr;

    if (!ensureMount())
        return READ_RETRY;

    char path[256];
    if (!makePath(kConfigPath, path, sizeof(path)))
        return READ_RETRY;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return READ_RETRY;

    FSFileHandle h;
    FSStatus r = FSOpenFile(&client, &cmd, path, "r", &h, FS_ERROR_FLAG_ALL);
    if (r == FS_STATUS_NOT_FOUND) {
        endFs(&client);
        return READ_MISSING;
    }
    if (r != FS_STATUS_OK) {
        endFs(&client);
        return READ_RETRY;
    }

    FSStat st;
    ReadResult result = READ_ERROR;
    if (FSGetStatFile(&client, &cmd, h, &st, FS_ERROR_FLAG_ALL) == FS_STATUS_OK &&
        st.size > 0 && st.size < maxBytes) {
        char* buf = (char*)malloc(st.size + 1);
        if (buf) {
            FSStatus n = FSReadFile(&client, &cmd, (uint8_t*)buf, 1, st.size, h, 0,
                                    FS_ERROR_FLAG_ALL);
            if (n >= 0) {
                buf[(uint32_t)n] = '\0';
                *outText = buf;
                result = READ_OK;
            } else {
                free(buf);
            }
        }
    }

    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return result;
}

bool SaveConfig(const char* text)
{
    if (!text || !ensureMount())
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;

    ensureRelDir(&client, &cmd, kRootDir);

    char path[256];
    if (!makePath(kConfigPath, path, sizeof(path))) {
        endFs(&client);
        return false;
    }

    FSFileHandle h;
    bool ok = false;
    FSStatus openR = FSOpenFile(&client, &cmd, path, "w", &h, FS_ERROR_FLAG_ALL);
    if (openR == FS_STATUS_OK) {
        uint32_t len = (uint32_t)strlen(text);
        FSStatus n = writeAligned(&client, &cmd, h, text, len);
        FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
        ok = n == (FSStatus)len;
        if (!ok)
            OSReport("[tphd_tools][storage] SaveConfig write failed: len=%u FSWriteFile=%d\n",
                     (unsigned)len, (int)n);
    } else {
        OSReport("[tphd_tools][storage] SaveConfig open '%s' failed: FSStatus=%d\n",
                 path, (int)openR);
    }

    endFs(&client);
    return ok;
}

bool PrepareLog()
{
    if (!ensureMount())
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;

    ensureRelDir(&client, &cmd, kRootDir);

    char logPath[256];
    char oldPath[256];
    if (!makePath(kLogPath, logPath, sizeof(logPath)) ||
        !makePath(kOldLogPath, oldPath, sizeof(oldPath))) {
        endFs(&client);
        return false;
    }

    // Rotate: drop the previous .old, move current log to .old (best-effort).
    FSRemove(&client, &cmd, oldPath, FS_ERROR_FLAG_ALL);
    FSRename(&client, &cmd, logPath, oldPath, FS_ERROR_FLAG_ALL);

    FSFileHandle h;
    FSStatus openR = FSOpenFile(&client, &cmd, logPath, "w", &h, FS_ERROR_FLAG_ALL);
    if (openR != FS_STATUS_OK) {
        OSReport("[tphd_tools][storage] PrepareLog open '%s' failed: FSStatus=%d\n",
                 logPath, (int)openR);
        endFs(&client);
        return false;
    }
    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return true;
}

bool AppendLog(const char* text, uint32_t size)
{
    if (!text || size == 0 || !ensureMount())
        return false;

    char path[256];
    if (!makePath(kLogPath, path, sizeof(path)))
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;

    // Append mode: the SD is a real FAT volume (unlike nn::save), so "a" works and
    // writes land at end of file. Open it, write the line, close.
    FSFileHandle h;
    FSStatus openR = FSOpenFile(&client, &cmd, path, "a", &h, FS_ERROR_FLAG_ALL);
    if (openR != FS_STATUS_OK) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            OSReport("[tphd_tools][storage] AppendLog open '%s' failed: FSStatus=%d\n",
                     path, (int)openR);
        }
        endFs(&client);
        return false;
    }

    FSStatus n = writeAligned(&client, &cmd, h, text, size);
    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);

    if (n != (FSStatus)size) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            OSReport("[tphd_tools][storage] AppendLog write failed: size=%u FSWriteFile=%d\n",
                     (unsigned)size, (int)n);
        }
        return false;
    }
    return true;
}

bool SaveState(const char* folder, const char* name, const void* image, uint32_t size)
{
    if (!name || !image || size == 0 || !ensureMount())
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;

    char relativeDir[128];
    if (!buildStateDir(folder, relativeDir, sizeof(relativeDir))) {
        endFs(&client);
        return false;
    }
    ensureRelDir(&client, &cmd, relativeDir);

    char path[256];
    if (!buildStatePath(folder, name, path, sizeof(path))) {
        endFs(&client);
        return false;
    }

    FSFileHandle h;
    bool ok = false;
    if (FSOpenFile(&client, &cmd, path, "w", &h, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        FSStatus n = writeAligned(&client, &cmd, h, image, size);
        FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
        ok = n == (FSStatus)size;
    }

    endFs(&client);
    return ok;
}

ReadResult LoadState(const char* folder, const char* name, void* outImage, uint32_t size,
                     uint32_t* outRead)
{
    if (!name || !outImage || size == 0)
        return READ_ERROR;
    if (outRead)
        *outRead = 0;
    if (!ensureMount())
        return READ_RETRY;

    char path[256];
    if (!buildStatePath(folder, name, path, sizeof(path)))
        return READ_ERROR;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return READ_RETRY;

    FSFileHandle h;
    FSStatus r = FSOpenFile(&client, &cmd, path, "r", &h, FS_ERROR_FLAG_ALL);
    if (r == FS_STATUS_NOT_FOUND) {
        endFs(&client);
        return READ_MISSING;
    }
    if (r != FS_STATUS_OK) {
        endFs(&client);
        return READ_ERROR;
    }

    FSStatus n = FSReadFile(&client, &cmd, (uint8_t*)outImage, 1, size, h, 0,
                            FS_ERROR_FLAG_ALL);
    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);
    if (n < 0)
        return READ_ERROR;
    if (outRead)
        *outRead = (uint32_t)n;
    return READ_OK;
}

bool DeleteState(const char* folder, const char* name)
{
    char path[256];
    if (!ensureMount() || !buildStatePath(folder, name, path, sizeof(path)))
        return false;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return false;

    bool ok = FSRemove(&client, &cmd, path, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
    endFs(&client);
    return ok;
}

int ListStates(const char* folder, StateEntry* outEntries, int maxEntries)
{
    if (!outEntries || maxEntries <= 0 || !ensureMount())
        return 0;

    char relativeDir[128];
    if (!buildStateDir(folder, relativeDir, sizeof(relativeDir)))
        return 0;
    char path[256];
    if (!makePath(relativeDir, path, sizeof(path)))
        return 0;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return 0;

    if (!folder || !folder[0])
        ensureRelDir(&client, &cmd, relativeDir);

    FSDirectoryHandle dh;
    if (FSOpenDir(&client, &cmd, path, &dh, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) {
        endFs(&client);
        return 0;
    }

    int count = 0;
    FSDirectoryEntry ent;
    while (count < maxEntries &&
           FSReadDir(&client, &cmd, dh, &ent, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if (!endsWithBin(ent.name))
            continue;

        StateEntry* e = &outEntries[count++];
        size_t stemLen = strlen(ent.name) - 4;
        if (stemLen >= sizeof(e->name))
            stemLen = sizeof(e->name) - 1;
        memcpy(e->name, ent.name, stemLen);
        e->name[stemLen] = '\0';

        size_t labelLen = stemLen;
        if (labelLen >= sizeof(e->label))
            labelLen = sizeof(e->label) - 1;
        memcpy(e->label, ent.name, labelLen);
        e->label[labelLen] = '\0';
    }

    FSCloseDir(&client, &cmd, dh, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return count;
}

int ListStateFolders(StateFolder* outFolders, int maxFolders)
{
    if (!outFolders || maxFolders <= 0 || !ensureMount())
        return 0;

    char path[256];
    if (!makePath(kStateDir, path, sizeof(path)))
        return 0;

    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return 0;
    ensureRelDir(&client, &cmd, kStateDir);

    FSDirectoryHandle dh;
    if (FSOpenDir(&client, &cmd, path, &dh, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) {
        endFs(&client);
        return 0;
    }

    int count = 0;
    FSDirectoryEntry ent;
    while (FSReadDir(&client, &cmd, dh, &ent, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if (!(ent.info.flags & FS_STAT_DIRECTORY) ||
            !validStateFolder(ent.name) || !ent.name[0] ||
            strlen(ent.name) >= sizeof(outFolders[0].name))
            continue;

        int insert = 0;
        while (insert < count && strcmp(outFolders[insert].name, ent.name) < 0)
            ++insert;
        if (insert >= maxFolders)
            continue;
        int newCount = count < maxFolders ? count + 1 : count;
        for (int i = newCount - 1; i > insert; --i)
            outFolders[i] = outFolders[i - 1];
        strncpy(outFolders[insert].name, ent.name,
                sizeof(outFolders[insert].name) - 1);
        outFolders[insert].name[sizeof(outFolders[insert].name) - 1] = '\0';
        count = newCount;
    }

    FSCloseDir(&client, &cmd, dh, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return count;
}

ReadResult LoadGameSave(const char* name, void* outImage, uint32_t size, uint32_t* outRead)
{
    if (!name || !outImage || size == 0)
        return READ_ERROR;
    if (outRead)
        *outRead = 0;
    if (!ensureMount())
        return READ_RETRY;

    char path[256];
    if (!buildGameSavePath(name, path, sizeof(path)))
        return READ_ERROR;
    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return READ_RETRY;
    FSFileHandle h;
    FSStatus r = FSOpenFile(&client, &cmd, path, "r", &h, FS_ERROR_FLAG_ALL);
    if (r == FS_STATUS_NOT_FOUND) {
        endFs(&client);
        return READ_MISSING;
    }
    if (r != FS_STATUS_OK) {
        endFs(&client);
        return READ_ERROR;
    }
    FSStatus n = FSReadFile(&client, &cmd, (uint8_t*)outImage, 1, size, h, 0,
                            FS_ERROR_FLAG_ALL);
    FSCloseFile(&client, &cmd, h, FS_ERROR_FLAG_ALL);
    endFs(&client);
    if (n < 0)
        return READ_ERROR;
    if (outRead)
        *outRead = (uint32_t)n;
    return READ_OK;
}

int ListGameSaves(StateEntry* outEntries, int maxEntries)
{
    if (maxEntries < 0 || !ensureMount())
        return 0;
    char path[256];
    if (!makePath(kGameSaveDir, path, sizeof(path)))
        return 0;
    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return 0;
    ensureRelDir(&client, &cmd, kGameSaveDir);
    FSDirectoryHandle dh;
    if (FSOpenDir(&client, &cmd, path, &dh, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) {
        endFs(&client);
        return 0;
    }

    int count = 0;
    FSDirectoryEntry ent;
    while (FSReadDir(&client, &cmd, dh, &ent, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if ((ent.info.flags & FS_STAT_DIRECTORY) || !endsWithDat(ent.name))
            continue;
        if (outEntries && count < maxEntries) {
            StateEntry* e = &outEntries[count];
            size_t stemLen = strlen(ent.name) - 4;
            if (stemLen >= sizeof(e->name))
                stemLen = sizeof(e->name) - 1;
            memcpy(e->name, ent.name, stemLen);
            e->name[stemLen] = '\0';
            strncpy(e->label, e->name, sizeof(e->label) - 1);
            e->label[sizeof(e->label) - 1] = '\0';
        }
        ++count;
    }
    FSCloseDir(&client, &cmd, dh, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return count;
}

int ListSplitFiles(SplitFileEntry* outEntries, int maxEntries)
{
    if (!outEntries || maxEntries <= 0 || !ensureMount())
        return 0;
    int count = 0;
    FSClient client;
    FSCmdBlock cmd;
    if (!beginFs(&client, &cmd))
        return 0;

    char path[256];
    if (!makePath(kSplitDir, path, sizeof(path))) {
        endFs(&client);
        return 0;
    }
    FSDirectoryHandle dh;
    if (FSOpenDir(&client, &cmd, path, &dh, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) {
        endFs(&client);
        return 0;
    }
    FSDirectoryEntry ent;
    while (FSReadDir(&client, &cmd, dh, &ent, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if ((ent.info.flags & FS_STAT_DIRECTORY) || !endsWithJson(ent.name) ||
            count >= maxEntries)
            continue;
        char relative[160];
        int pn = snprintf(relative, sizeof(relative), "%s/%s", kSplitDir, ent.name);
        if (pn <= 0 || pn >= (int)sizeof(relative))
            continue;

        SplitFileEntry e = {};
        snprintf(e.path, sizeof(e.path), "%s", relative);
        size_t stem = strlen(ent.name) - 5;
        if (stem >= sizeof(e.name))
            stem = sizeof(e.name) - 1;
        memcpy(e.name, ent.name, stem);
        e.name[stem] = '\0';

        int insert = count;
        while (insert > 0 && strcmp(outEntries[insert - 1].name, e.name) > 0) {
            outEntries[insert] = outEntries[insert - 1];
            --insert;
        }
        outEntries[insert] = e;
        ++count;
    }
    FSCloseDir(&client, &cmd, dh, FS_ERROR_FLAG_ALL);
    endFs(&client);
    return count;
}

ReadResult LoadSplitFile(const char* relativePath, char** outText, uint32_t maxBytes)
{
    if (!validSplitPath(relativePath))
        return READ_ERROR;
    return loadTextPath(relativePath, outText, maxBytes);
}

ReadResult LoadSplitHistory(const char* key, char** outText, uint32_t maxBytes)
{
    if (!validHistoryKey(key))
        return READ_ERROR;
    char relative[192];
    int n = snprintf(relative, sizeof(relative), "%s/%s.json", kSplitHistoryDir, key);
    if (n <= 0 || n >= (int)sizeof(relative))
        return READ_ERROR;
    return loadTextPath(relative, outText, maxBytes);
}

bool SaveSplitHistory(const char* key, const char* text)
{
    if (!validHistoryKey(key))
        return false;
    char relative[192];
    int n = snprintf(relative, sizeof(relative), "%s/%s.json", kSplitHistoryDir, key);
    return n > 0 && n < (int)sizeof(relative) &&
           saveTextPath(kSplitHistoryDir, relative, text);
}

ReadResult LoadSplitGolds(const char* key, char** outText, uint32_t maxBytes)
{
    if (!validHistoryKey(key))
        return READ_ERROR;
    char relative[192];
    int n = snprintf(relative, sizeof(relative), "%s/%s.json", kSplitGoldDir, key);
    if (n <= 0 || n >= (int)sizeof(relative))
        return READ_ERROR;
    return loadTextPath(relative, outText, maxBytes);
}

bool SaveSplitGolds(const char* key, const char* text)
{
    if (!validHistoryKey(key))
        return false;
    char relative[192];
    int n = snprintf(relative, sizeof(relative), "%s/%s.json", kSplitGoldDir, key);
    return n > 0 && n < (int)sizeof(relative) &&
           saveTextPath(kSplitGoldDir, relative, text);
}

} // namespace Storage
