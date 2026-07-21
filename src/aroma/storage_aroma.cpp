// storage_aroma.cpp -- SD-card backend for the Aroma/WPS build.
#include "storage.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include <coreinit/debug.h>     // OSReport (storage failures precede the file log)

namespace Storage {

// SD-card root for paths. WUPS_USE_WUT_DEVOPTAB() (see aroma/plugin.cpp) installs
// wut's devoptab, which registers a single device "fs:" with the SD mounted at
// /vol/external01 -- there is NO "sd:" device in this stack. So files live under
// "fs:/vol/external01/...". (Aroma grants plugins full FS access, so unlike the
// Cemu RPL no permission bypass / explicit mount is needed here.)
static const char* kSdRoot = "fs:/vol/external01";

static const char* kRootDir = "tphd_tools";
static const char* kConfigPath = "tphd_tools/config.json";
static const char* kLogPath = "tphd_tools/log.txt";
static const char* kOldLogPath = "tphd_tools/log.txt.old";
static const char* kStateDir = "tphd_tools/savestates";
static const char* kGameSaveDir = "tphd_tools/saves";
static const char* kSplitHistoryDir = "tphd_tools/split_times";
static const char* kSplitGoldDir = "tphd_tools/split_golds";
static const char* kSplitDir = "tphd_tools/splits";

static const char* trimLeadingSlashes(const char* p)
{
    while (*p == '/' || *p == '\\')
        ++p;
    return p;
}

static bool makePath(const char* relative, char* out, int outSize)
{
    if (!relative || !out || outSize <= 0)
        return false;
    int n = snprintf(out, outSize, "%s/%s", kSdRoot, trimLeadingSlashes(relative));
    if (n <= 0 || n >= outSize)
        return false;
    for (char* p = out; *p; ++p) {
        if (*p == '\\')
            *p = '/';
    }
    return true;
}

static bool dirExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool makeOneDir(const char* path)
{
    if (dirExists(path))
        return true;
    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return true;

    // Failures here happen before the file logger can work, so mirror the first
    // one to OSReport (visible via Aroma's logging module) for field debugging.
    static bool reported = false;
    if (!reported) {
        reported = true;
        OSReport("[tphd_tools][storage] mkdir '%s' failed (errno=%d)\n",
                 path, errno);
    }
    return false;
}

static bool ensureDir(const char* relativeDir)
{
    char full[256];
    if (!makePath(relativeDir, full, sizeof(full)))
        return false;

    // Create only OUR directories: component-walk the part below the SD root.
    // Never stat/mkdir the mount root's own components -- on real hardware
    // mkdir("fs:/vol") fails with an error other than EEXIST (unlike Cemu's
    // host FS), which aborted the walk before tphd_tools was ever created and
    // silently broke the log/config/save-state storage. The Cemu backend's
    // makeDirs() skips the mount root for the same reason.
    char* start = full + strlen(kSdRoot);
    while (*start == '/')
        ++start;

    for (char* p = start; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (!makeOneDir(full)) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    return makeOneDir(full);
}

static bool ensureParentDir(const char* relativePath)
{
    char dir[192];
    int n = snprintf(dir, sizeof(dir), "%s", relativePath ? relativePath : "");
    if (n <= 0 || n >= (int)sizeof(dir))
        return false;

    for (int i = n - 1; i >= 0; --i) {
        if (dir[i] == '/' || dir[i] == '\\') {
            dir[i] = '\0';
            return ensureDir(dir);
        }
    }
    return true;
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
    if (!name || !name[0] || !out || outSize <= 0)
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
    if (!relativePath || !outText || maxBytes < 2)
        return READ_ERROR;
    *outText = nullptr;
    char path[256];
    if (!makePath(relativePath, path, sizeof(path)))
        return READ_ERROR;
    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ReadResult result = READ_ERROR;
    if (size > 0 && size < (long)maxBytes) {
        char* text = (char*)malloc((size_t)size + 1);
        if (text) {
            size_t n = fread(text, 1, (size_t)size, f);
            text[n] = '\0';
            *outText = text;
            result = READ_OK;
        }
    }
    fclose(f);
    return result;
}

static bool saveTextPath(const char* relativeDir, const char* relativePath, const char* text)
{
    if (!relativeDir || !relativePath || !text || !ensureDir(relativeDir))
        return false;
    char path[256];
    if (!makePath(relativePath, path, sizeof(path)))
        return false;
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    size_t len = strlen(text);
    size_t n = fwrite(text, 1, len, f);
    fclose(f);
    return n == len;
}

static bool buildGameSavePath(const char* name, char* out, int outSize)
{
    if (!name || !name[0] || !out || outSize <= 0)
        return false;
    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\')
            return false;
    }
    char relative[160];
    int n = snprintf(relative, sizeof(relative), "%s/%s.dat", kGameSaveDir, name);
    return n > 0 && n < (int)sizeof(relative) && makePath(relative, out, outSize);
}

bool EnsureReady()
{
    return ensureDir(kRootDir) && ensureDir(kStateDir) && ensureDir(kGameSaveDir) &&
           ensureDir(kSplitHistoryDir) && ensureDir(kSplitGoldDir) &&
           ensureDir(kSplitDir);
}

ReadResult LoadConfig(char** outText, uint32_t maxBytes)
{
    if (!outText)
        return READ_ERROR;
    *outText = nullptr;

    char path[256];
    if (!makePath(kConfigPath, path, sizeof(path)))
        return READ_RETRY;

    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ReadResult result = READ_ERROR;
    if (size > 0 && size < (long)maxBytes) {
        char* buf = (char*)malloc((size_t)size + 1);
        if (buf) {
            size_t n = fread(buf, 1, (size_t)size, f);
            buf[n] = '\0';
            *outText = buf;
            result = READ_OK;
        }
    }

    fclose(f);
    return result;
}

bool SaveConfig(const char* text)
{
    if (!text || !ensureParentDir(kConfigPath))
        return false;

    char path[256];
    if (!makePath(kConfigPath, path, sizeof(path)))
        return false;

    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    size_t len = strlen(text);
    size_t n = fwrite(text, 1, len, f);
    fclose(f);
    return n == len;
}

bool PrepareLog()
{
    if (!ensureDir(kRootDir))
        return false;

    char logPath[256];
    char oldPath[256];
    if (!makePath(kLogPath, logPath, sizeof(logPath)) ||
        !makePath(kOldLogPath, oldPath, sizeof(oldPath))) {
        return false;
    }

    remove(oldPath);
    rename(logPath, oldPath);

    FILE* f = fopen(logPath, "wb");
    if (!f) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            OSReport("[tphd_tools][storage] create '%s' failed (errno=%d)\n",
                     logPath, errno);
        }
        return false;
    }
    fclose(f);
    return true;
}

bool AppendLog(const char* text, uint32_t size)
{
    if (!text || size == 0 || !ensureDir(kRootDir))
        return false;

    char path[256];
    if (!makePath(kLogPath, path, sizeof(path)))
        return false;

    FILE* f = fopen(path, "ab");
    if (!f)
        return false;
    size_t n = fwrite(text, 1, size, f);
    fclose(f);
    return n == size;
}

bool SaveState(const char* folder, const char* name, const void* image, uint32_t size)
{
    char relativeDir[128];
    if (!name || !image || size == 0 ||
        !buildStateDir(folder, relativeDir, sizeof(relativeDir)) ||
        !ensureDir(relativeDir))
        return false;

    char path[256];
    if (!buildStatePath(folder, name, path, sizeof(path)))
        return false;

    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    size_t n = fwrite(image, 1, size, f);
    fclose(f);
    return n == size;
}

ReadResult LoadState(const char* folder, const char* name, void* outImage, uint32_t size,
                     uint32_t* outRead)
{
    if (!name || !outImage || size == 0)
        return READ_ERROR;
    if (outRead)
        *outRead = 0;

    char path[256];
    if (!buildStatePath(folder, name, path, sizeof(path)))
        return READ_ERROR;

    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    size_t n = fread(outImage, 1, size, f);
    fclose(f);
    if (outRead)
        *outRead = (uint32_t)n;
    return READ_OK;
}

bool DeleteState(const char* folder, const char* name)
{
    char path[256];
    if (!buildStatePath(folder, name, path, sizeof(path)))
        return false;
    return remove(path) == 0;
}

int ListStates(const char* folder, StateEntry* outEntries, int maxEntries)
{
    char relativeDir[128];
    if (!outEntries || maxEntries <= 0 ||
        !buildStateDir(folder, relativeDir, sizeof(relativeDir)))
        return 0;
    if ((!folder || !folder[0]) && !ensureDir(relativeDir))
        return 0;

    char path[256];
    if (!makePath(relativeDir, path, sizeof(path)))
        return 0;

    DIR* dh = opendir(path);
    if (!dh)
        return 0;

    int count = 0;
    struct dirent* ent;
    while (count < maxEntries && (ent = readdir(dh)) != nullptr) {
        const char* name = ent->d_name;
        if (!endsWithBin(name))
            continue;

        StateEntry* e = &outEntries[count++];
        size_t stemLen = strlen(name) - 4;
        if (stemLen >= sizeof(e->name))
            stemLen = sizeof(e->name) - 1;
        memcpy(e->name, name, stemLen);
        e->name[stemLen] = '\0';

        size_t labelLen = stemLen;
        if (labelLen >= sizeof(e->label))
            labelLen = sizeof(e->label) - 1;
        memcpy(e->label, name, labelLen);
        e->label[labelLen] = '\0';
    }

    closedir(dh);
    return count;
}

int ListStateFolders(StateFolder* outFolders, int maxFolders)
{
    if (!outFolders || maxFolders <= 0 || !ensureDir(kStateDir))
        return 0;

    char root[256];
    if (!makePath(kStateDir, root, sizeof(root)))
        return 0;
    DIR* dh = opendir(root);
    if (!dh)
        return 0;

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dh)) != nullptr) {
        if (!validStateFolder(ent->d_name) || !ent->d_name[0] ||
            strlen(ent->d_name) >= sizeof(outFolders[0].name))
            continue;

        char child[256];
        struct stat st;
        int n = snprintf(child, sizeof(child), "%s/%s", root, ent->d_name);
        if (n <= 0 || n >= (int)sizeof(child) || stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        int insert = 0;
        while (insert < count && strcmp(outFolders[insert].name, ent->d_name) < 0)
            ++insert;
        if (insert >= maxFolders)
            continue;
        int newCount = count < maxFolders ? count + 1 : count;
        for (int i = newCount - 1; i > insert; --i)
            outFolders[i] = outFolders[i - 1];
        strncpy(outFolders[insert].name, ent->d_name,
                sizeof(outFolders[insert].name) - 1);
        outFolders[insert].name[sizeof(outFolders[insert].name) - 1] = '\0';
        count = newCount;
    }

    closedir(dh);
    return count;
}

ReadResult LoadGameSave(const char* name, void* outImage, uint32_t size, uint32_t* outRead)
{
    if (!name || !outImage || size == 0)
        return READ_ERROR;
    if (outRead)
        *outRead = 0;

    char path[256];
    if (!buildGameSavePath(name, path, sizeof(path)))
        return READ_ERROR;
    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    size_t n = fread(outImage, 1, size, f);
    fclose(f);
    if (outRead)
        *outRead = (uint32_t)n;
    return READ_OK;
}

int ListGameSaves(StateEntry* outEntries, int maxEntries)
{
    if (maxEntries < 0 || !ensureDir(kGameSaveDir))
        return 0;
    char path[256];
    if (!makePath(kGameSaveDir, path, sizeof(path)))
        return 0;
    DIR* dh = opendir(path);
    if (!dh)
        return 0;

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dh)) != nullptr) {
        if (!endsWithDat(ent->d_name))
            continue;
        if (outEntries && count < maxEntries) {
            StateEntry* e = &outEntries[count];
            size_t stemLen = strlen(ent->d_name) - 4;
            if (stemLen >= sizeof(e->name))
                stemLen = sizeof(e->name) - 1;
            memcpy(e->name, ent->d_name, stemLen);
            e->name[stemLen] = '\0';
            strncpy(e->label, e->name, sizeof(e->label) - 1);
            e->label[sizeof(e->label) - 1] = '\0';
        }
        ++count;
    }
    closedir(dh);
    return count;
}

int ListSplitFiles(SplitFileEntry* outEntries, int maxEntries)
{
    if (!outEntries || maxEntries <= 0)
        return 0;
    int count = 0;
    char path[256];
    if (!makePath(kSplitDir, path, sizeof(path)))
        return 0;
    DIR* dh = opendir(path);
    if (!dh)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(dh)) != nullptr) {
        if (!endsWithJson(ent->d_name) || count >= maxEntries)
            continue;
        char relative[160];
        int pn = snprintf(relative, sizeof(relative), "%s/%s", kSplitDir, ent->d_name);
        if (pn <= 0 || pn >= (int)sizeof(relative))
            continue;

        SplitFileEntry e = {};
        snprintf(e.path, sizeof(e.path), "%s", relative);
        size_t stem = strlen(ent->d_name) - 5;
        if (stem >= sizeof(e.name))
            stem = sizeof(e.name) - 1;
        memcpy(e.name, ent->d_name, stem);
        e.name[stem] = '\0';
        int insert = count;
        while (insert > 0 && strcmp(outEntries[insert - 1].name, e.name) > 0) {
            outEntries[insert] = outEntries[insert - 1];
            --insert;
        }
        outEntries[insert] = e;
        ++count;
    }
    closedir(dh);
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
