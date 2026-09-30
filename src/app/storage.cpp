#include "core/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace Storage {
static const char kRoot[] = "fs:/vol/external01/tphd_tools";
static const char kConfigPath[] = "fs:/vol/external01/tphd_tools/config.json";

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

static ReadResult readText(const char* path, char** outText, uint32_t maxBytes)
{
    if (!outText)
        return READ_ERROR;
    *outText = nullptr;
    if (!EnsureReady())
        return READ_RETRY;

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

static bool writeText(const char* path, const char* text)
{
    if (!text || !EnsureReady())
        return false;
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const size_t len = strlen(text);
    const bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    return ok;
}

ReadResult LoadConfig(char** outText, uint32_t maxBytes)
{
    return readText(kConfigPath, outText, maxBytes);
}

bool SaveConfig(const char* text)
{
    return writeText(kConfigPath, text);
}
}
