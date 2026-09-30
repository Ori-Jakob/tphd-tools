#pragma once

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

ReadResult LoadConfig(char** outText, uint32_t maxBytes);
bool SaveConfig(const char* text);
}
