#pragma once

struct cJSON;

namespace Cheats {
namespace Movement {
enum Rate {
    CLIMB_SPEED = 0,
    CLIMB_HEIGHT,
    BLOCK_SPEED,
    CHAIN_SPEED,
    CRAWL_SPEED,
    ROLL_SPEED,
    TIME_SPEED,
    RATE_COUNT
};

float       GetRate(Rate rate);
void        SetRate(Rate rate, float value);
float       RateMin(Rate rate);
float       RateMax(Rate rate);
const char* RateName(Rate rate);
const char* RateControlId(Rate rate);

float GetTimeOfDay();
void  SetTimeOfDay(float degrees);

bool InstantTextEnabled();
void SetInstantTextEnabled(bool enabled);

void Tick();
void OnApplicationStart();
void ResetToDefaults();

void LoadConfig(const cJSON* root);
void SaveConfig(cJSON* root);
}
}
