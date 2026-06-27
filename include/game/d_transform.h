// game/d_transform.h -- Link's human<->wolf "quick transform" + the error SE.
//
// Ports the MegaCheats quick-transform cheat (TwilightPrincessHD/Cheats/
// MegaCheats/patch_cheats.asm) and dusklight's daAlink_c::handleQuickTransform.
// MegaCheats codecave-replaces the Z-button transform handler; we instead poll
// the combo from the present hook (like our other cheats) and call the same
// engine routine. Guards mirror MegaCheats: block while holding the Ball & Chain
// and honour daMidna_c::checkMetamorphoseEnableBase (Midna ride / evil light /
// event bits / NPC proximity). On a blocked attempt we play the system error SE.
//
// Addresses are TPHD Zelda.rpx (US v81), verified in Ghidra.
#pragma once

#include "game/types.h"
#include "game/f_op_actor.h"
#include "game/d_com_inf_game.h"   // dComIfGp_getPlayer
#include "game/z2_audio.h"         // GAME_ADDR_z2AudioMgrPtr

// daAlink_c field offsets used by the transform guards. These are LITERAL TPHD
// offsets taken from the game's own Z-transform handler
// daAlink_c::checkTransformAction @ 0x0203a2b8 (so no GCN-offset drift):
#define DPLAYER_OFF_STATUS0   0x574u    // u32 status flags (&4 midna, &0x1000 magne boots)
#define DPLAYER_OFF_EQUIPITEM 0x2FDCu   // u16 currently-equipped item id
#define DPLAYER_OFF_XFORM_ST  0x301Au   // u8 transform sub-state: 0/5 idle, 1/2 busy
#define DPLAYER_OFF_MODEFLG   0x3218u   // u32 mModeFlg
#define DPLAYER_OFF_ACCHFLG   0x19ACu   // u32 ground-collision flags (&0x20 = ground hit)
// Flag values.
#define DITEM_IRONBALL          0x42u      // Ball & Chain (transforming with it crashes)
#define DPLAYER_ACCH_GROUNDHIT  0x20u      // mLinkAcch ground-contact bit
#define DPLAYER_MODE_FLY        0x70C52u   // MODE_PLAYER_FLY (+ related airborne modes)
#define DPLAYER_MODE_NOGNDCHK   0x40000u   // mode where the game skips the ground test
#define DPLAYER_STATUS_MAGNE    0x1000u    // magne boots (clings to walls/ceilings)

static inline u16 dPlayer_getEquipItem(fopAc_ac_c* link)
{
    return *(volatile u16*)((u8*)link + DPLAYER_OFF_EQUIPITEM);
}

// daAlink_c::procCoMetamorphoseInit(link, param) @ 0x02034888 -- kicks off the
// human<->wolf transform proc on Link (param byte = 0, as the Z-handler uses).
typedef int (*daAlink_procCoMetamorphoseInit_t)(void* link, u8 param);
#define daAlink_procCoMetamorphoseInit ((daAlink_procCoMetamorphoseInit_t)0x02034888u)

// daMidna_c::checkMetamorphoseEnableBase() @ 0x0247c484 -- void (reads the live
// Link actor + globals internally). 1 = transform allowed, 0 = blocked.
typedef int (*daMidna_checkMetamorphoseEnableBase_t)(void);
#define daMidna_checkMetamorphoseEnableBase ((daMidna_checkMetamorphoseEnableBase_t)0x0247c484u)

// ---- Z2 audio: play the system error SE -------------------------------------
// Z2AudioMgr::seStart @ 0x02c1d980. Register layout (from the MegaCheats hook +
// the decomp): r3=this, r4=&soundId (BY POINTER), r5..r8 = 0, f1=f2=1, f3=f4=-1.
#define Z2SE_SYS_ERROR          0x4Au
typedef int (*Z2_seStart_t)(void* mgr, u32* id, void* p2, int a, int b, int c,
                            float f1, float f2, float f3, float f4);
#define Z2_seStart ((Z2_seStart_t)0x02c1d980u)

static inline void dPlayer_playErrorSE(void)
{
    void* mgr = *(void* volatile*)GAME_ADDR_z2AudioMgrPtr;
    if (!mgr)
        return;
    u32 id = Z2SE_SYS_ERROR;
    Z2_seStart(mgr, &id, 0, 0, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f);
}

// True when Link's ground-collision reports floor contact this frame. Reads the
// same acch flag the transform guard uses. Handy after a save/stage load to tell
// when the room's floor has actually streamed in under him.
static inline bool dPlayer_onGround(fopAc_ac_c* link)
{
    if (!link)
        return false;
    u32 acch = *(volatile u32*)((u8*)link + DPLAYER_OFF_ACCHFLG);
    return (acch & DPLAYER_ACCH_GROUNDHIT) != 0;
}

// Whether Link may transform right now. Mirrors the guards the game's own Z
// handler (daAlink_c::checkTransformAction @ 0x0203a2b8) applies, so quick
// transform can't fire mid-air, while flying, mid-action, etc.
static inline bool dPlayer_canTransform(fopAc_ac_c* link)
{
    if (!link)
        return false;

    u8  state   = *(volatile u8*) ((u8*)link + DPLAYER_OFF_XFORM_ST);
    u32 modeFlg = *(volatile u32*)((u8*)link + DPLAYER_OFF_MODEFLG);
    u32 status  = *(volatile u32*)((u8*)link + DPLAYER_OFF_STATUS0);

    if (state != 0 && state != 5)                       // busy / already transforming
        return false;
    if (dPlayer_getEquipItem(link) == DITEM_IRONBALL)   // Ball & Chain would crash it
        return false;

    // Require ground contact + not flying -- but the game skips this test while
    // on magne boots (wall/ceiling cling) or in an attention mode, so we do too.
    if ((modeFlg & DPLAYER_MODE_NOGNDCHK) == 0 && (status & DPLAYER_STATUS_MAGNE) == 0) {
        u32 acch = *(volatile u32*)((u8*)link + DPLAYER_OFF_ACCHFLG);
        if ((acch & DPLAYER_ACCH_GROUNDHIT) == 0)       // not on the ground -> in the air
            return false;
        if ((modeFlg & DPLAYER_MODE_FLY) != 0)          // MODE_PLAYER_FLY etc.
            return false;
    }

    // Engine's comprehensive base guard: Midna present + riding, no evil light,
    // correct event state, no blocking NPC nearby.
    if (!daMidna_checkMetamorphoseEnableBase())
        return false;

    return true;
}

// Attempt a quick transform. Plays the error SE and returns false when blocked.
static inline bool dPlayer_quickTransform(fopAc_ac_c* link)
{
    if (!dPlayer_canTransform(link)) {
        dPlayer_playErrorSE();
        return false;
    }
    daAlink_procCoMetamorphoseInit(link, 0);
    return true;
}
