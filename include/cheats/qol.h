// Quality-of-life movement and day-clock modifiers backed by TPHD hooks.

#pragma once

namespace Cheats {
namespace QoL {

void DrawMenu();
void Tick();
void OnApplicationStart();
void OnApplicationEnd();

float GetClimbingSpeedMultiplier();
void  SetClimbingSpeedMultiplier(float value);
float GetClimbHeightMultiplier();
void  SetClimbHeightMultiplier(float value);
float GetBlockPushSpeedMultiplier();
void  SetBlockPushSpeedMultiplier(float value);
float GetCrawlSpeedMultiplier();
void  SetCrawlSpeedMultiplier(float value);
float GetRollSpeedMultiplier();
void  SetRollSpeedMultiplier(float value);
float GetTimeSpeedMultiplier();
void  SetTimeSpeedMultiplier(float value);

} // namespace QoL
} // namespace Cheats
