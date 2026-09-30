#include "app/code_hooks.h"

#include "core/logger.h"

#include <coreinit/cache.h>

extern "C" {
void tphdSuperClawshotFailureHook();
void tphdClawshotBossShootSpeedHook();
void tphdClawshotReturnSpeedHook();
void tphdClawshotShootSpeedF30Hook();
void tphdClawshotRangeF29Hook();
void tphdClawshotPullSpeedF31Hook();
void tphdClawshotPullSpeedF13Hook();
void tphdClawshotRangeF1Hook();
void tphdClawshotShootSpeedF1Hook();
void tphdInfiniteSpinnerTimeHook();
void tphdSpinnerSpeedHook();
void tphdRemoteBombFuseHook();
void tphdUnrestrictedBombsHook();
void tphdUnrestrictedItemsHook();
void tphdUnrestrictedSwordHook();
void tphdFastIronBootsAnimeHook();
void tphdFastIronBootsStoreF13Hook();
void tphdFastIronBootsStoreF30Hook();
void tphdAlwaysGreatSpinSkillHook();
void tphdAlwaysGreatSpinHealthHook();
void tphdDamageReceivedHook();
void tphdDamageGivenHook();
void tphdRupeeChangeHook();
void tphdNoFallDamageHook();
void tphdAlwaysFairyRevivalHook();
void tphdStarGameClearHook();
void tphdCanoeScoreSkipHook();
void tphdBoatScoreGain2Hook();
void tphdBoatScoreGain1Hook();
void tphdTimerFreezeHook();
void tphdGoatInstantWinHook();
void tphdFruitComboKeepHook();
void tphdEasySumoHook();
void tphdClimbingSpeedReturnHook();
void tphdClimbProbeHeightHook();
void tphdClimbDeltaHeightHook();
void tphdCrawlSpeedReturnHook();
void tphdRollFrontSpeedHook();
void tphdTimeRateHook();
void tphdAlternateTimeRateHook();
void tphdDoBeginInstantTextHook();
void tphdDoIsReadyInstantTextHook();
void tphdDoTagInstantTextHook();
void tphdIsSendInstantTextHook();
void tphdInstantTextOutnowAcceptHook();
void tphdWolfClimbAnimationHook();
void tphdWolfClimbParamHook();
void tphdHumanLedgeClimbAnimationHook();
void tphdBlockPushSpeedReturnHook();
void tphdBlockPullDurationHook();
void tphdBlockPushDurationHook();
void tphdBemosDurationHook();
void tphdBemosPhaseHook();
void tphdChainWallDurationHook();
void tphdChainWallPhaseHook();
void tphdChainWallElapsedHook();
void tphdHumanChainMaxSpeedBlendHook();
void tphdHumanChainMaxSpeedActionHook();
void tphdWolfChainMaxSpeedActionHook();
void tphdHumanChainAnimationHeavyHook();
void tphdHumanChainAnimationNormalHook();
void tphdWolfChainAnimationHeavyHook();
void tphdWolfChainAnimationNormalHook();
void tphdLv4ChandelierCurveHook();
void tphdCameraNextTypeHook();
}

namespace App {
namespace CodeHooks {
enum Shape : uint8_t { CALL, JUMP, CALL_EXP, JUMP_EXP };

struct Site {
    const char* name;
    uint32_t    addr;
    uint32_t    expected;
    Shape       shape;
    void      (*hook)();
};

// Order is the pack's _pkh_ table order in Cemu/graphicspack/patch_tphd_tools.asm.
static const Site kSites[] = {
    { "clawshotFailure",     0x0202B12Cu, 0x38600000u, CALL, tphdSuperClawshotFailureHook },
    { "clawshotBossShoot",   0x020592D0u, 0xC3C473F0u, CALL, tphdClawshotBossShootSpeedHook },
    { "clawshotReturn",      0x020592E8u, 0xC3EC73F0u, CALL, tphdClawshotReturnSpeedHook },
    { "clawshotShootF30",    0x020592F0u, 0xC3CB73BCu, CALL, tphdClawshotShootSpeedF30Hook },
    { "clawshotRangeF29",    0x020592F8u, 0xC3A777A0u, CALL, tphdClawshotRangeF29Hook },
    { "clawshotPullF31",     0x0205932Cu, 0xC3E47480u, CALL, tphdClawshotPullSpeedF31Hook },
    { "clawshotPullF13",     0x020423B8u, 0xC1A57480u, CALL, tphdClawshotPullSpeedF13Hook },
    { "clawshotRangeF1",     0x0209CF84u, 0xC02B77A0u, CALL, tphdClawshotRangeF1Hook },
    { "clawshotShootF1a",    0x02059C94u, 0xC02A73BCu, CALL, tphdClawshotShootSpeedF1Hook },
    { "clawshotShootF1b",    0x02059D34u, 0xC02A73BCu, CALL, tphdClawshotShootSpeedF1Hook },
    { "clawshotShootF1c",    0x02059DC4u, 0xC02A73BCu, CALL, tphdClawshotShootSpeedF1Hook },
    { "spinnerTime",         0x0288D8ACu, 0x3967FFFFu, CALL, tphdInfiniteSpinnerTimeHook },
    { "spinnerSpeed",        0x020B3128u, 0xC02C751Cu, CALL, tphdSpinnerSpeedHook },
    { "remoteBombFuse",      0x0248802Cu, 0x3BDEFFFFu, CALL, tphdRemoteBombFuseHook },
    { "noBombLimit",         0x02050C5Cu, 0x4080F698u, CALL, tphdUnrestrictedBombsHook },
    { "unrestrictedItems",   0x0201B314u, 0x38600000u, JUMP, tphdUnrestrictedItemsHook },
    { "unrestrictedSword",   0x02050EB0u, 0x4BFCA355u, CALL, tphdUnrestrictedSwordHook },
    { "ironBootsAnimeA",     0x02023648u, 0x4BFFF591u, CALL, tphdFastIronBootsAnimeHook },
    { "ironBootsAnimeB",     0x02023884u, 0x4BFFF355u, CALL, tphdFastIronBootsAnimeHook },
    { "ironBootsAnimeC",     0x0202BDDCu, 0x4BFF6DFDu, CALL, tphdFastIronBootsAnimeHook },
    { "ironBootsSpeedA",     0x020639D8u, 0xD1BE34F4u, CALL, tphdFastIronBootsStoreF13Hook },
    { "ironBootsSpeedB",     0x020639F0u, 0xD1BE34F4u, CALL, tphdFastIronBootsStoreF13Hook },
    { "ironBootsSpeedC",     0x02063A34u, 0xD1BE34F4u, CALL, tphdFastIronBootsStoreF13Hook },
    { "ironBootsSpeedD",     0x02063A44u, 0xD3DE34F4u, CALL, tphdFastIronBootsStoreF30Hook },
    { "greatSpinSkill",      0x0203AC14u, 0x41820014u, JUMP, tphdAlwaysGreatSpinSkillHook },
    { "greatSpinHealth",     0x0203AC24u, 0x41820020u, JUMP, tphdAlwaysGreatSpinHealthHook },
    { "damageReceived",      0x02A1C664u, 0xED29402Au, CALL, tphdDamageReceivedHook },
    { "damageGiven",         0x02904118u, 0x7D074050u, CALL, tphdDamageGivenHook },
    { "rupeeChange",         0x02A1DB74u, 0x819F6280u, CALL, tphdRupeeChangeHook },
    { "noFallDamageA",       0x0208C398u, 0x4BFFFA7Du, CALL, tphdNoFallDamageHook },
    { "noFallDamageB",       0x020A1C34u, 0x4BFEA1E1u, CALL, tphdNoFallDamageHook },
    { "noFallDamageC",       0x020BF018u, 0x4BFCCDFDu, CALL, tphdNoFallDamageHook },
    { "fairyRevival",        0x0203152Cu, 0x48A74F35u, CALL, tphdAlwaysFairyRevivalHook },
    { "starGameClear",       0x024DC584u, 0x80630E0Cu, JUMP, tphdStarGameClearHook },
    { "canoeScoreA",         0x021A37B4u, 0x4182007Cu, JUMP, tphdCanoeScoreSkipHook },
    { "canoeScoreB",         0x021A37E8u, 0x41820048u, JUMP, tphdCanoeScoreSkipHook },
    { "rapidsGain2",         0x0287CF1Cu, 0x39680002u, CALL, tphdBoatScoreGain2Hook },
    { "rapidsGain1",         0x0287CF54u, 0x39680001u, CALL, tphdBoatScoreGain1Hook },
    { "timerFreeze",         0x02AC6388u, 0x907D0118u, CALL_EXP, tphdTimerFreezeHook },
    { "goatInstantWin",      0x024A7424u, 0x7C095040u, CALL, tphdGoatInstantWinHook },
    { "fruitComboKeep",      0x026BEEE0u, 0x7C0B0040u, CALL, tphdFruitComboKeepHook },
    { "easySumo",            0x02667158u, 0x83E1000Cu, CALL, tphdEasySumoHook },
    { "climbSpeedLadder",    0x0203F5D8u, 0x4E800020u, JUMP, tphdClimbingSpeedReturnHook },
    { "climbSpeedWall",      0x020A8EE0u, 0x4E800020u, JUMP, tphdClimbingSpeedReturnHook },
    { "climbSpeedShimmy",    0x020A6658u, 0x4E800020u, JUMP, tphdClimbingSpeedReturnHook },
    { "climbProbeA",         0x02033FDCu, 0xEFF7B02Au, CALL, tphdClimbProbeHeightHook },
    { "climbProbeB",         0x02034064u, 0xEFF7B02Au, CALL, tphdClimbProbeHeightHook },
    { "climbProbeC",         0x020340E8u, 0xEFF7B02Au, CALL, tphdClimbProbeHeightHook },
    { "climbProbeD",         0x02034164u, 0xEFF7B02Au, CALL, tphdClimbProbeHeightHook },
    { "climbDelta",          0x0203443Cu, 0xECCBB828u, CALL, tphdClimbDeltaHeightHook },
    { "crawlSpeed",          0x02053684u, 0x4E800020u, JUMP, tphdCrawlSpeedReturnHook },
    { "rollSpeed",           0x0204D1FCu, 0x801F0574u, CALL, tphdRollFrontSpeedHook },
    { "timeRate",            0x02974A60u, 0xC11E1248u, CALL, tphdTimeRateHook },
    { "timeRateAlt",         0x02974CE0u, 0xC15E1248u, CALL, tphdAlternateTimeRateHook },
    { "textDoBegin",         0x02A58584u, 0x9421FFD0u, JUMP, tphdDoBeginInstantTextHook },
    { "textDoIsReady",       0x02A58B54u, 0x7C0802A6u, JUMP, tphdDoIsReadyInstantTextHook },
    { "textDoTag",           0x02A594B0u, 0x7C0802A6u, JUMP, tphdDoTagInstantTextHook },
    { "textIsSend",          0x02A67D18u, 0x7C0802A6u, JUMP, tphdIsSendInstantTextHook },
    { "textOutnowAccept",    0x02A67990u, 0x4BFB10C1u, CALL, tphdInstantTextOutnowAcceptHook },
    { "ledgeWolfAnime",      0x0203FF7Cu, 0xC02A7090u, CALL_EXP, tphdWolfClimbAnimationHook },
    { "ledgeWolfParam",      0x0202D624u, 0xC0250004u, JUMP_EXP, tphdWolfClimbParamHook },
    { "ledgeHumanAnime",     0x020A6348u, 0xC02B7014u, CALL_EXP, tphdHumanLedgeClimbAnimationHook },
    { "blockPushRate",       0x02095820u, 0x4E800020u, JUMP_EXP, tphdBlockPushSpeedReturnHook },
    { "blockPull",           0x027D11F4u, 0xA80C000Au, CALL_EXP, tphdBlockPullDurationHook },
    { "blockPush",           0x027D1254u, 0xA8EC0004u, CALL_EXP, tphdBlockPushDurationHook },
    { "bemosDuration",       0x026C515Cu, 0x3800000Du, CALL_EXP, tphdBemosDurationHook },
    { "bemosPhase",          0x026C5160u, 0xC1ACCF98u, CALL_EXP, tphdBemosPhaseHook },
    { "chainWallDuration",   0x026FD65Cu, 0x3960000Du, JUMP_EXP, tphdChainWallDurationHook },
    { "chainWallPhase",      0x026FD670u, 0x398009D8u, JUMP_EXP, tphdChainWallPhaseHook },
    { "chainWallElapsed",    0x026FD8A0u, 0x210B000Du, CALL_EXP, tphdChainWallElapsedHook },
    { "chainMaxBlend",       0x0202C30Cu, 0xC14C7474u, CALL_EXP, tphdHumanChainMaxSpeedBlendHook },
    { "chainMaxHuman",       0x02041CDCu, 0xC00A7474u, CALL_EXP, tphdHumanChainMaxSpeedActionHook },
    { "chainMaxWolf",        0x020523DCu, 0xC00C7474u, CALL_EXP, tphdWolfChainMaxSpeedActionHook },
    { "chainAnimHumanHeavy", 0x0202C458u, 0xC0297314u, CALL_EXP, tphdHumanChainAnimationHeavyHook },
    { "chainAnimHuman",      0x0202C474u, 0xFC20E890u, CALL_EXP, tphdHumanChainAnimationNormalHook },
    { "chainAnimWolfHeavy",  0x02051B08u, 0xC02A7314u, CALL_EXP, tphdWolfChainAnimationHeavyHook },
    { "chainAnimWolf",       0x02051B4Cu, 0xFC20F890u, CALL_EXP, tphdWolfChainAnimationNormalHook },
    { "chandelierA",         0x02795234u, 0x800BCF8Cu, CALL_EXP, tphdLv4ChandelierCurveHook },
    { "chandelierB",         0x0279525Cu, 0x800BCF8Cu, CALL_EXP, tphdLv4ChandelierCurveHook },
    { "cameraNextTypeA",     0x028DF9A8u, 0x7C651B78u, CALL, tphdCameraNextTypeHook },
    { "cameraNextTypeB",     0x028DF9D4u, 0x7C651B78u, CALL, tphdCameraNextTypeHook },
};

static const uint32_t kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

static RplHook s_hooks[kSiteCount];

uint32_t Count() { return kSiteCount; }

#ifdef TPHD_TOOLS_EXPERIMENTAL
static const bool kExperimental = true;
#else
static const bool kExperimental = false;
#endif

static bool wanted(const Site& s)
{
    return kExperimental || (s.shape != CALL_EXP && s.shape != JUMP_EXP);
}

bool Install(const RplHost* host)
{
    if (!host)
        return false;

    uint32_t applied = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < kSiteCount; ++i) {
        const Site& s = kSites[i];
        if (!wanted(s))
            continue;
        ++total;
        RplHook& h = s_hooks[i];
        h.name        = s.name;
        h.linkAddr    = s.addr;
        h.expected    = s.expected;
        h.shape       = s.shape == CALL || s.shape == CALL_EXP ? RPL_SHAPE_CALL : RPL_SHAPE_JUMP;
        h.flags       = RPL_HOOK_OPTIONAL;
        h.hook        = (const void*)s.hook;
        h.replacement = 0;
        h.original    = nullptr;
        h.module      = nullptr;
        h.function    = nullptr;
        if (host->addHook(host, &h) == 0)
            ++applied;
        else
            Logger::LogWarn("[hooks] %s at %08X did not apply", s.name, (unsigned)s.addr);
    }
    Logger::Log("[hooks] %u of %u code hooks applied", (unsigned)applied, (unsigned)total);
    return applied == total;
}

bool Publish(uint32_t* table, uint32_t count)
{
    if (!table || count != kSiteCount) {
        Logger::LogError("[hooks] pack lists %u code hooks, the rpl has %u; pack and rpl "
                         "are from different builds", (unsigned)count, (unsigned)kSiteCount);
        return false;
    }
    for (uint32_t i = 0; i < kSiteCount; ++i)
        if (wanted(kSites[i]))
            table[i] = (uint32_t)(uintptr_t)kSites[i].hook;
    DCFlushRange(table, kSiteCount * sizeof(uint32_t));
    Logger::Log("[hooks] %u code hooks published to the pack", (unsigned)kSiteCount);
    return true;
}
}
}
