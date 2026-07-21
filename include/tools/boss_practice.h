// tools/boss_practice.h -- deterministic boss/miniboss encounter launcher.
#pragma once

namespace Tools {
namespace BossPractice {

void DrawMenuItem();
void DrawWindow(bool menuActive);
void Tick();
void OnApplicationStart();

bool IsEnabled();
void SetEnabled(bool enabled);

} // namespace BossPractice
} // namespace Tools

