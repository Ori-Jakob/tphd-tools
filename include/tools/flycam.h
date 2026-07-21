// tools/flycam.h -- free-fly camera (ported from the shipped FlyCam cheat).
//
// Enabled via a Tools-menu checkbox. While enabled, the rebindable Fly Cam
// hotkey (default ZL+ZR+L3+R3) toggles fly mode; in fly mode the game is frozen
// and the camera is driven by the sticks.
#pragma once

namespace Tools {
namespace FlyCam {

// Checkbox shown in Camera & HUD (enables/disables the feature).
void DrawMenuItem();

bool IsEnabled();
void SetEnabled(bool enabled);

bool IsActive();   // currently flying
void Stop();       // leave fly mode, releasing camera/freeze ownership
void OnApplicationStart();

// Called once per presented frame: reads the controller, runs the fly-cam
// state machine, and drives the game camera.
void Tick();

} // namespace FlyCam
} // namespace Tools
