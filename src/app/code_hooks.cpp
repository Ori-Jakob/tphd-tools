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
}

namespace App {
namespace CodeHooks {
enum Shape : uint8_t { CALL, JUMP };

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
    { "timerFreeze",         0x02AC6388u, 0x907D0118u, CALL, tphdTimerFreezeHook },
    { "goatInstantWin",      0x024A7424u, 0x7C095040u, CALL, tphdGoatInstantWinHook },
    { "fruitComboKeep",      0x026BEEE0u, 0x7C0B0040u, CALL, tphdFruitComboKeepHook },
    { "easySumo",            0x02667158u, 0x83E1000Cu, CALL, tphdEasySumoHook },
};

static const uint32_t kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

static RplHook s_hooks[kSiteCount];

uint32_t Count() { return kSiteCount; }

bool Install(const RplHost* host)
{
    if (!host)
        return false;

    uint32_t applied = 0;
    for (uint32_t i = 0; i < kSiteCount; ++i) {
        const Site& s = kSites[i];
        RplHook& h = s_hooks[i];
        h.name        = s.name;
        h.linkAddr    = s.addr;
        h.expected    = s.expected;
        h.shape       = s.shape == CALL ? RPL_SHAPE_CALL : RPL_SHAPE_JUMP;
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
    Logger::Log("[hooks] %u of %u code hooks applied", (unsigned)applied, (unsigned)kSiteCount);
    return applied == kSiteCount;
}

bool Publish(uint32_t* table, uint32_t count)
{
    if (!table || count != kSiteCount) {
        Logger::LogError("[hooks] pack lists %u code hooks, the rpl has %u; pack and rpl "
                         "are from different builds", (unsigned)count, (unsigned)kSiteCount);
        return false;
    }
    for (uint32_t i = 0; i < kSiteCount; ++i)
        table[i] = (uint32_t)(uintptr_t)kSites[i].hook;
    DCFlushRange(table, kSiteCount * sizeof(uint32_t));
    Logger::Log("[hooks] %u code hooks published to the pack", (unsigned)kSiteCount);
    return true;
}
}
}
