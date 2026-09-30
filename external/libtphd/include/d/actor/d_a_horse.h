#pragma once

#include "SSystem/SComponent/c_xyz.h"
#include "d/d_com_inf_game.h"
#include "d/d_procname.h"
#include "f_op/f_op_actor.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct daHorse_c : fopAc_ac_c {
    u8  field_0x0568[0x16FC - 0x0568];
    s16 mLashAccelerationTime;
    u8  field_0x16fe[0x1704 - 0x16FE];
    s16 mLashCount;
};

TPHD_ASSERT_OFFSET(daHorse_c, mLashAccelerationTime, 0x16FC);
TPHD_ASSERT_OFFSET(daHorse_c, mLashCount, 0x1704);

#define daHorse_LASH_COUNT_MAX 6

typedef void (*daHorse_setHorsePosAndAngle_t)(daHorse_c* horse, const cXyz* pos,
                                              s16 angle);
#define daHorse_setHorsePosAndAngle \
    ((daHorse_setHorsePosAndAngle_t)TPHD_ADDR_daHorse_setHorsePosAndAngle)

inline daHorse_c* dComIfGp_getHorse(void)
{
    return (daHorse_c*)dComIfGp_getHorseActor();
}

inline void daHorse_refillDashes(daHorse_c* horse)
{
    if (horse)
        horse->mLashCount = daHorse_LASH_COUNT_MAX;
}

inline void daHorse_holdDashSpeed(daHorse_c* horse)
{
    if (!horse)
        return;
    if (horse->mLashAccelerationTime < 2)
        horse->mLashAccelerationTime = 2;
}

inline void daHorse_setPosition(daHorse_c* horse, const cXyz* pos, s16 angle,
                                s8 roomNo)
{
    if (!horse || !pos)
        return;
    daHorse_setHorsePosAndAngle(horse, pos, angle);
    horse->old.roomNo = roomNo;
    horse->current.roomNo = roomNo;
    horse->speed.zero();
    horse->speedF = 0.0f;
}
