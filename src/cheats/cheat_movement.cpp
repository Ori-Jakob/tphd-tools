#include "cheats/cheat_movement.h"

#include "core/config.h"

#include "libtphd/reloc.h"

#include <coreinit/cache.h>
#include <stdint.h>

extern "C" {
volatile float g_tphdClimbingSpeedMultiplier = 1.0f;
volatile float g_tphdClimbHeightMultiplier = 1.0f;
volatile float g_tphdBlockPushSpeedMultiplier = 1.0f;
volatile uint32_t g_tphdBlockPushSpeedQuarters = 4u;
volatile float g_tphdBemosMovePhase = 32768.0f / 13.0f;
volatile float g_tphdChainPullSpeedMultiplier = 1.0f;
volatile uint32_t g_tphdChainPullSpeedQuarters = 4u;
volatile uint32_t g_tphdQoLFrameSerial = 0u;
volatile float g_tphdCrawlSpeedMultiplier = 1.0f;
volatile float g_tphdRollSpeedMultiplier = 1.0f;
volatile float g_tphdTimeSpeedMultiplier = 1.0f;
volatile uint8_t g_tphdInstantTextEnabled = 0;

enum {
    kSeqProcRefOff = 0x00,
    kSeqProcInstantFlagOff = 0xB2,
    kRefCharAllAlphaOff = 0x85C,
    kRefObjectPtrOff = 0x9C0,
    kRefSendTimerOff = 0x9D6,
    kRefDemoFrameOff = 0x9B4,
    kRefSendFlagOff = 0x1C4C,
    kMsgSeqProcOff = 0x11C,
    kMsgDemoElapsedOff = 0x17A,
    kMsgFukiKindOff = 0x196,
    kMsgSelectWordFlagOff = 0x4C6,
    kMsgShopWaitTimerOff = 0x4C9,
    kHowlFukiKind = 0x11,
    kSendFlagFlow = 3,
    kSendFlagDemo = 4,
};

typedef uint32_t (*GetHoldBFn)(int);
typedef uint32_t (*MenuAcceptFn)();

static bool holdingB()
{
    return ((GetHoldBFn)0x02AF817Cu)(0) != 0;
}

static void applyInstantText(void* seqProc)
{
    if (!g_tphdInstantTextEnabled || !seqProc || !holdingB())
        return;
    *((uint8_t*)seqProc + kSeqProcInstantFlagOff) = 1;
    void* ref = *(void**)((uint8_t*)seqProc + kSeqProcRefOff);
    if (!ref)
        return;
    *(int16_t*)((uint8_t*)ref + kRefSendTimerOff) = 0;
    *(float*)((uint8_t*)ref + kRefCharAllAlphaOff) = 1.0f;
    void* msgObj = *(void**)((uint8_t*)ref + kRefObjectPtrOff);
    if (msgObj) {
        const uint32_t demoFrame = *(uint32_t*)((uint8_t*)ref + kRefDemoFrameOff);
        *(int16_t*)((uint8_t*)msgObj + kMsgDemoElapsedOff) =
            demoFrame > 0x7FFFu ? (int16_t)0x7FFF : (int16_t)demoFrame;
    }
}

uint32_t tphdInstantTextOutnowAccept()
{
    if (((MenuAcceptFn)0x02A18A50u)() != 0)
        return 1;
    return g_tphdInstantTextEnabled && holdingB() ? 1 : 0;
}

void tphdInstantTextPrepareSequence(void* seqProc)
{
    applyInstantText(seqProc);
}

void tphdInstantTextPrepareIsSend(void* msgObj)
{
    if (msgObj)
        applyInstantText(*(void**)((uint8_t*)msgObj + kMsgSeqProcOff));
}

uint32_t tphdInstantTextFilterIsSend(uint32_t result, void* msgObj)
{
    if (result != 0 || !g_tphdInstantTextEnabled || !msgObj || !holdingB())
        return result;
    if (*((uint8_t*)msgObj + kMsgShopWaitTimerOff) != 0 ||
        *((uint8_t*)msgObj + kMsgSelectWordFlagOff) != 0 ||
        *((uint8_t*)msgObj + kMsgFukiKindOff) == kHowlFukiKind)
        return 0;
    void* seqProc = *(void**)((uint8_t*)msgObj + kMsgSeqProcOff);
    if (!seqProc)
        return 0;
    void* ref = *(void**)((uint8_t*)seqProc + kSeqProcRefOff);
    if (!ref)
        return 0;
    const uint8_t sendFlag = *((uint8_t*)ref + kRefSendFlagOff);
    if (sendFlag == kSendFlagFlow || sendFlag == kSendFlagDemo)
        return result;
    return 2;
}

struct ChandelierCurve {
    const void* actor;
    int         lastFrame;
    uint32_t    lastTick;
    uint32_t    quarters;
};

static ChandelierCurve s_chandeliers[8];

int tphdLv4ChandelierCurveDelta(const void* actor, int frame)
{
    static const int kCurve[10] = { 0, 0, 0, 0, 0, 1, 1, 2, 2, 4 };

    if (frame < 0)
        frame = 0;
    if (frame > 9)
        frame = 9;

    const uint32_t quarters = g_tphdChainPullSpeedQuarters;
    if (quarters == 4u)
        return kCurve[frame];

    const uint32_t now = g_tphdQoLFrameSerial;
    int slot = -1;
    int freeSlot = -1;
    int oldestSlot = 0;
    uint32_t oldestAge = 0u;
    for (int i = 0; i < 8; ++i) {
        if (s_chandeliers[i].actor == actor) {
            slot = i;
            break;
        }
        if (!s_chandeliers[i].actor && freeSlot < 0)
            freeSlot = i;
        const uint32_t age = now - s_chandeliers[i].lastTick;
        if (age >= oldestAge) {
            oldestAge = age;
            oldestSlot = i;
        }
    }
    if (slot < 0)
        slot = freeSlot >= 0 ? freeSlot : oldestSlot;

    ChandelierCurve& state = s_chandeliers[slot];
    const uint32_t age = now - state.lastTick;
    int delta = kCurve[frame];
    if (state.actor == actor && state.quarters == quarters && age <= 32u) {
        delta = 0;
        if (frame > state.lastFrame) {
            for (int i = state.lastFrame + 1; i <= frame; ++i)
                delta += kCurve[i];
        } else if (frame < state.lastFrame) {
            for (int i = state.lastFrame + 1; i < 10; ++i)
                delta += kCurve[i];
            for (int i = 0; i <= frame; ++i)
                delta += kCurve[i];
        }
    }

    state.actor = actor;
    state.lastFrame = frame;
    state.lastTick = now;
    state.quarters = quarters;
    return delta;
}
}

namespace Cheats {
namespace Movement {
struct RateEntry {
    const char* name;
    const char* controlId;
    const char* configKey;
    float       lo, hi;
};

static const RateEntry kRates[RATE_COUNT] = {
    { "Climbing Speed",              "gameplay.movement.climb_speed",  "moveClimbSpeed",  0.25f, 5.0f },
    { "Climb Height",                "gameplay.movement.climb_height", "moveClimbHeight", 0.25f, 4.0f },
    { "Block Push/Pull Speed",       "gameplay.movement.block_speed",  "moveBlockSpeed",  0.25f, 5.0f },
    { "Chain Pull Speed",            "gameplay.movement.chain_speed",  "moveChainSpeed",  0.25f, 5.0f },
    { "Crawling Speed",              "gameplay.movement.crawl_speed",  "moveCrawlSpeed",  0.25f, 5.0f },
    { "Front Roll Speed / Distance", "gameplay.movement.roll_speed",   "moveRollSpeed",   0.25f, 5.0f },
    { "Time Progression Speed",      "gameplay.world.time_speed",      "worldTimeSpeed",  0.0f, 20.0f },
};

static const uint32_t kSavedTime = 0x1014537Cu;
static const uint32_t kBemosFrames = 13u;

static float s_rates[RATE_COUNT];
static bool  s_instantText = false;

static bool valid(Rate r) { return r >= 0 && r < RATE_COUNT; }

float GetRate(Rate r)             { return valid(r) ? s_rates[r] : 1.0f; }
float RateMin(Rate r)             { return valid(r) ? kRates[r].lo : 0.0f; }
float RateMax(Rate r)             { return valid(r) ? kRates[r].hi : 1.0f; }
const char* RateName(Rate r)      { return valid(r) ? kRates[r].name : "?"; }
const char* RateControlId(Rate r) { return valid(r) ? kRates[r].controlId : ""; }

void SetRate(Rate r, float value)
{
    if (!valid(r))
        return;
    if (value < kRates[r].lo) value = kRates[r].lo;
    if (value > kRates[r].hi) value = kRates[r].hi;
    s_rates[r] = (float)(int)(value * 4.0f + 0.5f) * 0.25f;
}

bool InstantTextEnabled()               { return s_instantText; }
void SetInstantTextEnabled(bool enabled) { s_instantText = enabled; }

static volatile float* savedTime()
{
    return (volatile float*)TPHD_DATA(kSavedTime);
}

float GetTimeOfDay()
{
    const float value = *savedTime();
    return value >= 0.0f && value < 360.0f ? value : 0.0f;
}

void SetTimeOfDay(float degrees)
{
    if (degrees < 0.0f)
        degrees = 0.0f;
    if (degrees >= 360.0f)
        degrees = 359.999f;
    *savedTime() = degrees;
}

template <typename T>
static void publish(volatile T* target, T value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(T));
}

static uint32_t quarters(float rate)
{
    return (uint32_t)(rate * 4.0f + 0.5f);
}

static uint32_t scaledFrames(uint32_t frames, uint32_t speedQuarters)
{
    const uint32_t scaled = (frames * 4u + speedQuarters / 2u) / speedQuarters;
    return scaled > 0u ? scaled : 1u;
}

void Tick()
{
    g_tphdQoLFrameSerial = g_tphdQoLFrameSerial + 1u;
    publish<float>(&g_tphdClimbingSpeedMultiplier, s_rates[CLIMB_SPEED]);
    publish<float>(&g_tphdClimbHeightMultiplier, s_rates[CLIMB_HEIGHT]);
    publish<float>(&g_tphdCrawlSpeedMultiplier, s_rates[CRAWL_SPEED]);
    publish<float>(&g_tphdRollSpeedMultiplier, s_rates[ROLL_SPEED]);
    publish<float>(&g_tphdTimeSpeedMultiplier, s_rates[TIME_SPEED]);
    publish<uint8_t>(&g_tphdInstantTextEnabled, s_instantText ? 1 : 0);

#ifdef TPHD_TOOLS_EXPERIMENTAL
    const float block = s_rates[BLOCK_SPEED];
    const float chain = s_rates[CHAIN_SPEED];
#else
    const float block = 1.0f;
    const float chain = 1.0f;
#endif
    publish<float>(&g_tphdBlockPushSpeedMultiplier, block);
    publish<uint32_t>(&g_tphdBlockPushSpeedQuarters, quarters(block));
    publish<float>(&g_tphdBemosMovePhase,
                   32768.0f / (float)scaledFrames(kBemosFrames, quarters(block)));
    publish<float>(&g_tphdChainPullSpeedMultiplier, chain);
    if (g_tphdChainPullSpeedQuarters != quarters(chain)) {
        for (ChandelierCurve& c : s_chandeliers)
            c = {};
    }
    publish<uint32_t>(&g_tphdChainPullSpeedQuarters, quarters(chain));
}

void OnApplicationStart()
{
    g_tphdQoLFrameSerial = 0u;
    for (ChandelierCurve& c : s_chandeliers)
        c = {};
}

void ResetToDefaults()
{
    for (int i = 0; i < RATE_COUNT; ++i)
        s_rates[i] = 1.0f;
    s_instantText = false;
}

void LoadConfig(const cJSON* root)
{
    for (int i = 0; i < RATE_COUNT; ++i) {
        float value = s_rates[i];
        if (Config::ReadFloat(root, kRates[i].configKey, &value, kRates[i].lo, kRates[i].hi))
            SetRate((Rate)i, value);
    }
    Config::ReadBool(root, "worldInstantText", &s_instantText);
}

void SaveConfig(cJSON* root)
{
    for (int i = 0; i < RATE_COUNT; ++i)
        Config::AddNumber(root, kRates[i].configKey, s_rates[i]);
    Config::AddBool(root, "worldInstantText", s_instantText);
}
}
}
