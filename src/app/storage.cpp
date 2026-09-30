#include "core/storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace Storage {
static const char kRoot[] = "fs:/vol/external01/tphd_tools";
static const char kConfig[] = "config.json";

static bool s_ready = false;

const char* RootPath()
{
    return kRoot;
}

bool EnsureReady()
{
    if (s_ready)
        return true;

    struct stat st;
    if (stat(kRoot, &st) == 0 || mkdir(kRoot, 0777) == 0) {
        s_ready = true;
        return true;
    }
    return stat(kRoot, &st) == 0 && (s_ready = true);
}

bool MakePath(char* out, size_t size, const char* relative)
{
    if (!out || !size || !relative || strstr(relative, ".."))
        return false;
    const int n = relative[0] ? snprintf(out, size, "%s/%s", kRoot, relative)
                              : snprintf(out, size, "%s", kRoot);
    return n > 0 && (size_t)n < size;
}

bool EnsureDir(const char* relativeDir)
{
    if (!EnsureReady())
        return false;
    char path[256];
    if (!MakePath(path, sizeof(path), relativeDir))
        return false;

    for (char* p = path + strlen(kRoot) + 1; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(path, 0777);
        *p = '/';
    }
    struct stat st;
    return stat(path, &st) == 0 || mkdir(path, 0777) == 0;
}

ReadResult ReadText(const char* relative, char** outText, uint32_t maxBytes)
{
    if (!outText)
        return READ_ERROR;
    *outText = nullptr;
    if (!EnsureReady())
        return READ_RETRY;

    char path[256];
    if (!MakePath(path, sizeof(path), relative))
        return READ_ERROR;
    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return READ_ERROR;
    }
    const long size = ftell(f);
    if (size < 0 || (uint32_t)size > maxBytes) {
        fclose(f);
        return READ_ERROR;
    }
    rewind(f);

    char* text = (char*)malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return READ_ERROR;
    }
    const size_t read = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[read] = '\0';
    *outText = text;
    return READ_OK;
}

bool WriteText(const char* relative, const char* text)
{
    return text && WriteBinary(relative, text, (uint32_t)strlen(text));
}

ReadResult ReadBinary(const char* relative, void* out, uint32_t capacity, uint32_t* outSize)
{
    if (outSize)
        *outSize = 0;
    if (!out || !capacity)
        return READ_ERROR;
    if (!EnsureReady())
        return READ_RETRY;

    char path[256];
    if (!MakePath(path, sizeof(path), relative))
        return READ_ERROR;
    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return READ_ERROR;
    }
    const long size = ftell(f);
    if (size <= 0 || (uint32_t)size > capacity) {
        fclose(f);
        return READ_ERROR;
    }
    rewind(f);
    const size_t read = fread(out, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size)
        return READ_ERROR;
    if (outSize)
        *outSize = (uint32_t)read;
    return READ_OK;
}

bool WriteBinary(const char* relative, const void* data, uint32_t size)
{
    if (!data || !EnsureReady())
        return false;
    char path[256];
    if (!MakePath(path, sizeof(path), relative))
        return false;
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

bool Remove(const char* relative)
{
    char path[256];
    return MakePath(path, sizeof(path), relative) && remove(path) == 0;
}

bool ListDir(const char* relativeDir, DirVisitor visit, void* context)
{
    if (!visit || !EnsureReady())
        return false;
    char path[256];
    if (!MakePath(path, sizeof(path), relativeDir))
        return false;
    DIR* dir = opendir(path);
    if (!dir)
        return false;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        DirEntry e;
        snprintf(e.name, sizeof(e.name), "%.127s", ent->d_name);
        char child[256];
        const int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        e.isDir = n > 0 && n < (int)sizeof(child) && stat(child, &st) == 0 &&
                  S_ISDIR(st.st_mode);
        visit(e, context);
    }
    closedir(dir);
    return true;
}

ReadResult LoadConfig(char** outText, uint32_t maxBytes)
{
    return ReadText(kConfig, outText, maxBytes);
}

bool SaveConfig(const char* text)
{
    return WriteText(kConfig, text);
}
}
