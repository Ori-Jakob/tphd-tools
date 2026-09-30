#include "cheats/cheat_minigames.h"

#include "core/config.h"

#include <coreinit/cache.h>
#include <stdint.h>

extern "C" {
volatile uint8_t g_tphdStarGameInstantWinEnabled = 0;
volatile uint8_t g_tphdBoatNoPointLossEnabled = 0;
volatile uint8_t g_tphdBoatPointMultiplier = 1;
volatile uint8_t g_tphdFreezeTimerEnabled = 0;
volatile uint8_t g_tphdGoatInstantWinEnabled = 0;
volatile uint8_t g_tphdFruitComboKeepEnabled = 0;
volatile uint8_t g_tphdEasySumoEnabled = 0;
}

namespace {
struct TimerFreeze {
    void*    timer;
    uint64_t time;
    uint64_t start;
    uint64_t pause;
    uint64_t limit;
    bool     tracking;
};

TimerFreeze s_freeze = {};

const uint32_t kTimerStart = 0x110u;
const uint32_t kTimerTime  = 0x118u;
const uint32_t kTimerPause = 0x138u;
const uint32_t kTimerLimit = 0x140u;

uint64_t loadTicks(void* timer, uint32_t offset)
{
    volatile uint32_t* w = (volatile uint32_t*)((uint8_t*)timer + offset);
    return ((uint64_t)w[0] << 32) | w[1];
}

void storeTicks(void* timer, uint32_t offset, uint64_t ticks)
{
    volatile uint32_t* w = (volatile uint32_t*)((uint8_t*)timer + offset);
    w[0] = (uint32_t)(ticks >> 32);
    w[1] = (uint32_t)ticks;
}
}

extern "C" void tphdTimerFreezeStep(void* timer, uint32_t nowHigh, uint32_t nowLow)
{
    if (!g_tphdFreezeTimerEnabled || !timer) {
        s_freeze = {};
        return;
    }

    const uint64_t now = ((uint64_t)nowHigh << 32) | nowLow;
    const uint64_t previous = loadTicks(timer, kTimerTime);
    uint64_t start = loadTicks(timer, kTimerStart);
    const uint64_t pause = loadTicks(timer, kTimerPause);
    const uint64_t limit = loadTicks(timer, kTimerLimit);

    const bool sameRun = s_freeze.tracking && s_freeze.timer == timer &&
                         s_freeze.time == previous && s_freeze.start == start &&
                         s_freeze.pause == pause && s_freeze.limit == limit;
    if (sameRun && now >= previous) {
        const uint64_t shifted = start + (now - previous);
        if (shifted >= start) {
            start = shifted;
            storeTicks(timer, kTimerStart, start);
        }
    }

    s_freeze.timer = timer;
    s_freeze.time = now;
    s_freeze.start = start;
    s_freeze.pause = pause;
    s_freeze.limit = limit;
    s_freeze.tracking = true;
}

namespace Cheats {
namespace Minigames {
struct Entry {
    const char* name;
    const char* controlId;
    const char* configKey;
};

static const Entry kEntries[TOGGLE_COUNT] = {
    { "STAR Game Instant Win",         "gameplay.minigames.star_instant_win", "gameStarInstantWin" },
    { "Rapids Ride Never Lose Points", "gameplay.minigames.rapids_no_loss",   "gameRapidsNoLoss" },
    { "Goat Herding Instant Win",      "gameplay.minigames.goat_instant_win", "gameGoatInstantWin" },
    { "Fruit Combo Always Doubles",    "gameplay.minigames.fruit_combo_keep", "gameFruitComboKeep" },
    { "Easy Sumo Wrestling",           "gameplay.minigames.easy_sumo",        "gameEasySumo" },
    { "Freeze Timers",                 "gameplay.minigames.freeze_timers",    "gameFreezeTimers" },
};

static bool s_enabled[TOGGLE_COUNT];
static int  s_rapidsMultiplier = 1;

static bool valid(Toggle t) { return t >= 0 && t < TOGGLE_COUNT; }

bool IsEnabled(Toggle t)        { return valid(t) && s_enabled[t]; }
const char* Name(Toggle t)      { return valid(t) ? kEntries[t].name : "?"; }
const char* ControlId(Toggle t) { return valid(t) ? kEntries[t].controlId : ""; }

void SetEnabled(Toggle t, bool enabled)
{
    if (valid(t))
        s_enabled[t] = enabled;
}

int GetRapidsMultiplier() { return s_rapidsMultiplier; }

void SetRapidsMultiplier(int value)
{
    s_rapidsMultiplier = value < 1 ? 1 : value > 10 ? 10 : value;
}

static void publish(volatile uint8_t* target, uint8_t value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
}

void Tick(bool overlayHoldingPlay)
{
    publish(&g_tphdStarGameInstantWinEnabled, s_enabled[STAR_INSTANT_WIN] ? 1 : 0);
    publish(&g_tphdBoatNoPointLossEnabled, s_enabled[RAPIDS_NO_LOSS] ? 1 : 0);
    publish(&g_tphdBoatPointMultiplier, (uint8_t)s_rapidsMultiplier);
    publish(&g_tphdGoatInstantWinEnabled, s_enabled[GOAT_INSTANT_WIN] ? 1 : 0);
    publish(&g_tphdFruitComboKeepEnabled, s_enabled[FRUIT_COMBO_KEEP] ? 1 : 0);
    publish(&g_tphdEasySumoEnabled, s_enabled[EASY_SUMO] ? 1 : 0);
#ifdef TPHD_TOOLS_EXPERIMENTAL
    const bool holdTimers = s_enabled[FREEZE_TIMERS] || overlayHoldingPlay;
#else
    (void)overlayHoldingPlay;
    const bool holdTimers = false;
#endif
    if (!holdTimers)
        s_freeze = {};
    publish(&g_tphdFreezeTimerEnabled, holdTimers ? 1 : 0);
}

void ResetToDefaults()
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        s_enabled[i] = false;
    s_rapidsMultiplier = 1;
}

void LoadConfig(const cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::ReadBool(root, kEntries[i].configKey, &s_enabled[i]);
    Config::ReadInt(root, "gameRapidsMultiplier", &s_rapidsMultiplier, 1, 10);
}

void SaveConfig(cJSON* root)
{
    for (int i = 0; i < TOGGLE_COUNT; ++i)
        Config::AddBool(root, kEntries[i].configKey, s_enabled[i]);
    Config::AddNumber(root, "gameRapidsMultiplier", s_rapidsMultiplier);
}
}
}
