// storage.h -- build-specific persistent storage interface.
//
// The contract is intentionally backend-neutral:
//   Cemu/RPL uses the coreinit FS API against the emulated SD card.
//   Aroma/WPS uses stdio against the physical SD card.
#pragma once

#include <stdint.h>

namespace Storage {

enum ReadResult {
    READ_OK = 0,
    READ_MISSING,
    READ_RETRY,
    READ_ERROR,
};

struct StateEntry {
    char name[64];    // filename stem, no ".bin"
    char label[80];   // display label
};

struct StateFolder {
    char name[64];    // immediate child directory under tphd_tools/savestates
};

struct SplitFileEntry {
    char name[96];    // filename stem, no ".json"
    char path[160];   // SD-relative path used by LoadSplitFile
};

// Called from the present thread during startup/load retries.
bool EnsureReady();

// Config text is malloc'd by the backend and must be freed by the caller.
ReadResult LoadConfig(char** outText, uint32_t maxBytes);
bool SaveConfig(const char* text);

// Log file lives beside config at SD path tphd_tools/log.txt.
// PrepareLog rotates log.txt -> log.txt.old and creates a fresh log.txt.
bool PrepareLog();
bool AppendLog(const char* text, uint32_t size);

// `folder` is empty/null for the Main tab, otherwise one immediate child
// directory under tphd_tools/savestates.
bool SaveState(const char* folder, const char* name, const void* image, uint32_t size);
ReadResult LoadState(const char* folder, const char* name, void* outImage, uint32_t size,
                     uint32_t* outRead);
bool DeleteState(const char* folder, const char* name);
int ListStates(const char* folder, StateEntry* outEntries, int maxEntries);
int ListStateFolders(StateFolder* outFolders, int maxFolders);

// Raw game/debug save images stored under tphd_tools/saves/*.dat.
ReadResult LoadGameSave(const char* name, void* outImage, uint32_t size, uint32_t* outRead);
// Returns the total number of matching files and writes at most maxEntries.
// Pass nullptr, 0 to query the required entry count before allocating.
int ListGameSaves(StateEntry* outEntries, int maxEntries);

// Auto-split definitions stored under tphd_tools/splits.
int ListSplitFiles(SplitFileEntry* outEntries, int maxEntries);
ReadResult LoadSplitFile(const char* relativePath, char** outText, uint32_t maxBytes);

// Per-route PB history, stored under tphd_tools/split_times.
ReadResult LoadSplitHistory(const char* key, char** outText, uint32_t maxBytes);
bool SaveSplitHistory(const char* key, const char* text);

// Per-route segment golds, stored under tphd_tools/split_golds.
ReadResult LoadSplitGolds(const char* key, char** outText, uint32_t maxBytes);
bool SaveSplitGolds(const char* key, const char* text);

} // namespace Storage
