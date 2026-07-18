// game/f_op_actor.h -- the actor base, fopAc_ac_c.
//
// Only the fields we've verified in TPHD are named; everything else is padding.
// Offsets match the GameCube/dusklight `fopAc_ac_c` layout and were confirmed in
// TPHD's Zelda.rpx (FUN_0224ba9c reads current.pos@0x4D0, current.angle.y@0x4DE,
// shape_angle.y@0x4E6, speedF@0x52C). `speed`@0x4F8 is bracketed by two verified
// fields (pos@0x4D0 and speedF@0x52C both match GC), so it is unshifted too.
//
// Extend this struct as more offsets are reverse-engineered; the static_asserts
// below guarantee the named fields stay at their true offsets.
#pragma once

#include "game/types.h"

// position + angle + room, as packed in an actor (matches actor_place, 0x14).
typedef struct actor_place {
    cXyz  pos;       // 0x00
    csXyz angle;     // 0x0C
    s8    roomNo;    // 0x12
    u8    _pad0x13;  // 0x13
} actor_place;

typedef struct fopAc_ac_c {
    u8          _pad0x000[0x4A8];
    actor_place home;          // 0x4A8
    actor_place old;           // 0x4BC
    actor_place current;       // 0x4D0  (current.pos @0x4D0, current.angle.y @0x4DE)
    csXyz       shape_angle;   // 0x4E4  (visual facing; .y @0x4E6)
    u8          _pad0x4EA[0x4F8 - 0x4EA];
    cXyz        speed;         // 0x4F8  (velocity)
    u8          _pad0x504[0x52C - 0x504];
    f32         speedF;        // 0x52C  (scalar forward speed)
} fopAc_ac_c;

#ifdef __cplusplus
#include <cstddef>
static_assert(offsetof(actor_place, angle)   == 0x0C,  "actor_place.angle");
static_assert(sizeof(actor_place)            == 0x14,  "sizeof(actor_place)");
static_assert(offsetof(fopAc_ac_c, current)     == 0x4D0, "current");
static_assert(offsetof(fopAc_ac_c, shape_angle) == 0x4E4, "shape_angle");
static_assert(offsetof(fopAc_ac_c, speed)       == 0x4F8, "speed");
static_assert(offsetof(fopAc_ac_c, speedF)      == 0x52C, "speedF");
#endif

// tpgz-style convenience accessors.
static inline cXyz* fopAcM_GetPosition(fopAc_ac_c* ac) { return &ac->current.pos; }
static inline cXyz* fopAcM_GetSpeed(fopAc_ac_c* ac)    { return &ac->speed; }
static inline s16   fopAcM_GetShapeAngleY(fopAc_ac_c* ac) { return ac->shape_angle.y; }

// Link-specific fields used
// TPHD keeps daPy_py_c::mBodyAngle at 0x59C; the stats y-angle is its .y field.
// daAlink_c::mProcID drifted from TPGZ's 0x2FE8 to 0x305C in TPHD.
#define DAPY_OFF_BODY_ANGLE_Y   0x059Eu
#define DAALINK_OFF_ACTION_ID   0x305Cu
#define DAALINK_OFF_NORMAL_SPEED 0x3458u
#define DAALINK_ACTION_WAIT      3u

// TPHD daAlink_c::procWaitInit. Ghidra verifies that this calls the common
// process initializer with action 3, installs the wait process/mode, zeros
// mNormalSpeed, selects the idle animation, and synchronizes actor facing.
typedef int (*daAlink_procWaitInit_t)(fopAc_ac_c* link);
#define daAlink_procWaitInit ((daAlink_procWaitInit_t)0x020243f8u)

static inline s16 daPy_getLookAngleY(const fopAc_ac_c* link)
{
    return *(const volatile s16*)((const u8*)link + DAPY_OFF_BODY_ANGLE_Y);
}

static inline u16 daAlink_getActionID(const fopAc_ac_c* link)
{
    return *(const volatile u16*)((const u8*)link + DAALINK_OFF_ACTION_ID);
}

static inline void daAlink_clearMomentum(fopAc_ac_c* link)
{
    if (!link)
        return;
    link->speed.x = link->speed.y = link->speed.z = 0.0f;
    link->speedF = 0.0f;
    *(volatile f32*)((u8*)link + DAALINK_OFF_NORMAL_SPEED) = 0.0f;
}
