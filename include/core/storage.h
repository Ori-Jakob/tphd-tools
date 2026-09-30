#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Storage {
enum ReadResult {
    READ_OK = 0,
    READ_MISSING,
    READ_RETRY,
    READ_ERROR,
};

bool EnsureReady();
const char* RootPath();

bool MakePath(char* out, size_t size, const char* relative);
bool EnsureDir(const char* relativeDir);

ReadResult ReadText(const char* relative, char** outText, uint32_t maxBytes);
bool       WriteText(const char* relative, const char* text);
ReadResult ReadBinary(const char* relative, void* out, uint32_t capacity, uint32_t* outSize);
bool       WriteBinary(const char* relative, const void* data, uint32_t size);
bool       Remove(const char* relative);

struct DirEntry {
    char name[128];
    bool isDir;
};
typedef void (*DirVisitor)(const DirEntry& entry, void* context);
bool ListDir(const char* relativeDir, DirVisitor visit, void* context);

ReadResult LoadConfig(char** outText, uint32_t maxBytes);
bool SaveConfig(const char* text);
}
