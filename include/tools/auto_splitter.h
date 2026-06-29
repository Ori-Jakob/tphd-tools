// tools/auto_splitter.h -- in-game split timer driven by legacy Wii U split JSON.
#pragma once

namespace Tools {
namespace AutoSplitter {

void Initialize();
void Tick();
void DrawMenuItem();
void DrawWindow(bool menuActive);

bool IsEnabled();
void SetEnabled(bool enabled);
bool IsAutoStartEnabled();
void SetAutoStartEnabled(bool enabled);
bool IsLoadRemovalEnabled();
void SetLoadRemovalEnabled(bool enabled);
float GetDeltaPreviewSeconds();
void SetDeltaPreviewSeconds(float seconds);
bool IsInitialsWhenDeltaShownEnabled();
void SetInitialsWhenDeltaShownEnabled(bool enabled);
const char* GetSelectedPath();
void SetSelectedPath(const char* path);

} // namespace AutoSplitter
} // namespace Tools
