// game/d_ami_shutter.h -- Lakebed's drain-gate runtime state.
//
// TPHD Zelda.rpx FUN_026b91ec (daAmiShutter_c::create) proves that the type-0
// grate has no persistent completion switch: it normally creates open and the
// actor itself closes it after Link enters. The game's Deku Toad demo-skip
// handoff is the sole native alternate create path; it creates this same actor
// at its final closed transform/mode. Boss Practice uses the verified final
// fields below to materialize that post-close state without playing the room-
// entry door event or setting Deku Toad's separate demo-skip byte.
#pragma once

#include "game/d_actor.h"

#define DAMISHUTTER_OFF_MODE       0x05C8u
#define DAMISHUTTER_OFF_TYPE       0x05C9u
#define DAMISHUTTER_OFF_CLOSED_Z   0x05CCu
#define DAMISHUTTER_OFF_OPEN       0x05DDu

#define DAMISHUTTER_TYPE_EVENT     0u
#define DAMISHUTTER_MODE_CLOSE_END 4u

// daAmiShutter_c::setBaseMtx, verified in TPHD at FUN_026b9168.
typedef void (*daAmiShutter_setBaseMtx_t)(fopAc_ac_c* shutter);
#define daAmiShutter_setBaseMtx ((daAmiShutter_setBaseMtx_t)0x026b9168u)

static inline bool daAmiShutter_setClosed(fopAc_ac_c* shutter)
{
    if (!shutter || fopAcM_GetName(shutter) != FPCNM_OBJ_AMISHUTTER)
        return false;
    volatile u8* bytes = (volatile u8*)shutter;
    if (bytes[DAMISHUTTER_OFF_TYPE] != DAMISHUTTER_TYPE_EVENT)
        return false;

    // Natural close completion leaves current.pos.z at mPosZ, mMode at
    // MODE_CLOSE_END, and mOpen false. Rebuilding the base matrix publishes
    // the same final transform to the model/moving-background actor.
    shutter->current.pos.z = *(volatile f32*)(bytes + DAMISHUTTER_OFF_CLOSED_Z);
    bytes[DAMISHUTTER_OFF_MODE] = DAMISHUTTER_MODE_CLOSE_END;
    bytes[DAMISHUTTER_OFF_OPEN] = 0;
    daAmiShutter_setBaseMtx(shutter);
    return true;
}
