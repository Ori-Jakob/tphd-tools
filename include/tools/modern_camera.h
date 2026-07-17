// tools/modern_camera.h -- modern third-person camera (dusklight freeCamera).
//
// A port of dusklight's dCamera_c::freeCamera: the game's follow cameras keep
// doing everything they normally do -- target selection, follow distance,
// zoom, wall collision, transitions -- and only the view DIRECTION becomes
// right-stick driven once the stick is touched (no more auto-rotate behind
// Link). Implemented by patching the camera's engine dispatch table (like the
// dScnPly phase_1 hook): every engine entry gets a thin wrapper. The chase
// engine (and the ride engine, when the Epona option is on) runs the real
// engine and then rewrites the view-cache direction; every other engine
// passes through untouched apart from the optional global FOV scale. First
// person (R3), lock-on, talk, and cutscene cameras therefore stay vanilla.
#pragma once

#include <stdint.h>

namespace Tools {
namespace ModernCamera {

// Checkbox (+ inline tuning) shown in the Tools menu.
void DrawMenuItem();

bool IsEnabled();
void SetEnabled(bool enabled);
float GetSensitivityX();
void SetSensitivityX(float sensitivity);
float GetSensitivityY();
void SetSensitivityY(float sensitivity);
float GetFovScale();
void SetFovScale(float scale);
bool IsGlobalFovEnabled();          // apply the FOV scale under EVERY camera
void SetGlobalFovEnabled(bool enabled);
bool IsHorseEnabled();              // drive the ride (Epona) camera too
void SetHorseEnabled(bool enabled);
bool IsInvertXEnabled();
void SetInvertXEnabled(bool enabled);
bool IsInvertYEnabled();
void SetInvertYEnabled(bool enabled);

// Once per presented frame: installs/removes the engine-table hooks to match
// the enabled state and drops the manual-direction latch whenever no
// direction-owning engine ran (mirrors dusklight's dispatch-site reset).
void Tick();

// Aroma: called at ON_APPLICATION_START. The engine table lives in game data
// and is fresh in the new process; drop the stale installed/chain state so
// the hooks are re-installed cleanly.
void OnApplicationStart();

} // namespace ModernCamera
} // namespace Tools
