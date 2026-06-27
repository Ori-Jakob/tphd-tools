// game/d_horse.h -- Epona runtime access and transform helpers.
//
// TPHD Zelda.rpx (US v81), verified in Ghidra:
//   g_dComIfG_gameInfo.play.mPlayerPtr[1] @ 0x1014B5BC
//   daAlink_c::mRideStatus                @ Link + 0x301A
//   daHorse_c::setHorsePosAndAngleSubstance @ 0x02418B00
//
// mRideStatus is the ride-type enum (0 = on foot, 1 = horse, 2 = boar, ...). It
// lives at 0x301A: daAlink_c::initForceRideHorse (FUN_0202483c) writes
// `*(link + 0x301A) = 1`, and it sits one byte before field_0x2fab (0x301B, set
// to 0x13 by commonInitForceRideRein). This is the SAME byte d_transform.h reads
// as DPLAYER_OFF_XFORM_ST -- the transform guard blocks while it's nonzero (1/2)
// because you can't wolf-transform while mounted. (The earlier 0x2FFC was wrong:
// that's an unrelated s16, so we never detected the mount and saved horseRiding=0.)
//
// The GameCube offsets after daHorse_c's base shifted by four bytes in TPHD,
// but the engine routine handles those private fields (reins and cached angles)
// for us, so callers only work with the typed actor base.
#pragma once

#include "game/types.h"
#include "game/f_op_actor.h"

#define GAME_ADDR_mHorseActor 0x1014b5bcu

#define DPLAYER_OFF_RIDE_STATUS 0x301Au
#define DPLAYER_RIDETYPE_HORSE  1u

// d_a_horse profile id, from the TPHD runtime proc-name table (DAT_10131250,
// built by FUN_020096e0 from the {u16 id; char* name} array @ 0x100034a0).
// NOT 0x00EE -- that id is d_a_canoe (the fishing-pond boat) in TPHD, which is
// why dynamically spawning "Epona" by the vanilla id produced a canoe.
#define FPCNM_HORSE 0x00EF

typedef void (*daHorse_setHorsePosAndAngle_t)(fopAc_ac_c* horse, const cXyz* pos, s16 angle);
#define daHorse_setHorsePosAndAngle ((daHorse_setHorsePosAndAngle_t)0x02418b00u)

static inline fopAc_ac_c* dComIfGp_getHorseActor(void)
{
    return *(fopAc_ac_c* volatile*)GAME_ADDR_mHorseActor;
}

static inline bool dPlayer_isHorseRiding(const fopAc_ac_c* link)
{
    return link &&
           *(const volatile u8*)((const u8*)link + DPLAYER_OFF_RIDE_STATUS) ==
               DPLAYER_RIDETYPE_HORSE;
}

static inline void dHorse_setPosition(fopAc_ac_c* horse, const cXyz* pos, s16 angle, s8 roomNo)
{
    if (!horse || !pos)
        return;

    daHorse_setHorsePosAndAngle(horse, pos, angle);
    horse->old.roomNo = roomNo;
    horse->current.roomNo = roomNo;
    horse->speed.x = horse->speed.y = horse->speed.z = 0.0f;
    horse->speedF = 0.0f;
}
