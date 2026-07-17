// tools/save_state.h -- save-state loader.
//
// Captures the live game position (stage / room / layer / spawn + Link's
// position & facing) to a .bin under the active storage backend:
//   Cemu/RPL:  title SAVE path /tphd_tools/savestates/x.bin
//   Aroma/WPS: SD path tphd_tools/savestates/x.bin
// Future speedrun states should live under tphd_tools/savestates/speedrun/x.bin.
// File I/O runs on a background thread; the
// actual warp + position override is applied on the present thread via Tick().
#pragma once

#include "game/types.h"   // s8, s16

namespace Tools {
namespace SaveState {

// Checkbox shown in the Tools menu (toggles the window).
void DrawMenuItem();
void Initialize();

bool IsEnabled();
void SetEnabled(bool enabled);
bool IsPositionOverrideEnabled();
void SetPositionOverrideEnabled(bool enabled);
bool IsReloadLastHotkeyEnabled();
void SetReloadLastHotkeyEnabled(bool enabled);
const char* GetLastLoadedStateName();
void SetLastLoadedStateName(const char* name);
const char* GetLastLoadedStateFolder();
void SetLastLoadedStateFolder(const char* folder);

// The Save Loader window.
void DrawWindow(bool menuActive);

// Called once per presented frame: consumes a finished background load (queues
// the warp) and applies the pending Link-position override once the target
// stage has loaded.
void Tick();

// Aroma: called at ON_APPLICATION_START. The previous game process took the
// worker thread and any in-flight load with it; this drops that stale state so
// nothing from the last session (a pending warp, a handed-off snapshot, a
// wedged busy flag) can leak into the new one. Cemu front ends load the module
// fresh per boot and don't need it.
void OnApplicationStart();

// Called immediately before Zelda.rpx dScnPly::phase_1. This is the exact
// old-scene-gone/new-scene-not-started boundary used to install a pending save
// block safely. Front ends own the platform-specific hook and call this shared
// handler; calls made without a pending load are harmless.
void OnScenePhase1();

// Reload `stage` (room/spawn/layer) rebuilding the runtime from the CURRENT
// in-memory save block. If `pos` is non-null Link is forced there (facing
// `angle`) once the reload settles; otherwise the stage's spawn point places
// him. The Debug Save Loader uses this after applying a debug .dat into the info
// block. Drives the same warp + re-stamp sequence as a save-state load via Tick().
void BeginInPlaceReload(const char* stage, s8 room, s16 spawn, s8 layer,
                        const cXyz* pos, s16 angle);

// Load a raw GAME_DSV_SERIALIZED_SIZE-byte game/debug save image from external
// storage. The image is copied immediately; deserialization happens during the
// old scene's teardown, then the game resumes from dSv_player_return_place_c.
bool BeginGameSaveLoad(const char* sourceName, const void* image, uint32_t size);

} // namespace SaveState
} // namespace Tools
