#pragma once

#include <stdint.h>

namespace Cheats {
void OnApplicationStart();
void Tick(bool acceptInput);
bool WantsInputBlock(uint32_t held);

void ResetToDefaults();
}
