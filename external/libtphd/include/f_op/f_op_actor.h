#pragma once

#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"
#include "f_pc/f_pc_leaf.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct actor_place {
    cXyz  pos;
    csXyz angle;
    s8    roomNo;
    u8    field_0x13;
};

TPHD_ASSERT_OFFSET(actor_place, angle, 0x0C);
TPHD_ASSERT_OFFSET(actor_place, roomNo, 0x12);
TPHD_ASSERT_SIZE(actor_place, 0x14);

struct actor_attention_types {
    u8   distances[14];
    s16  field_0x0e;
    cXyz position;
    u32  flags;
};

TPHD_ASSERT_OFFSET(actor_attention_types, position, 0x10);
TPHD_ASSERT_SIZE(actor_attention_types, 0x20);

enum fopAc_Status_e {
    fopAcStts_NOEXEC_e  = 0x00000080,
    fopAcStts_CULL_e    = 0x00000100,
    fopAcStts_FREEZE_e  = 0x00000400,
    fopAcStts_CARRY_e   = 0x00002000,
    fopAcStts_NOPAUSE_e = 0x00020000,
    fopAcStts_NODRAW_e  = 0x01000000,
    fopAcStts_BOSS_e    = 0x04000000,
};

enum fopAc_Group_e {
    fopAc_ACTOR_e,
    fopAc_PLAYER_e,
    fopAc_ENEMY_e,
    fopAc_ENV_e,
    fopAc_NPC_e,
    fopAc_UNK_GROUP_5_e,
};

struct fopAc_ac_c : leafdraw_class {
    int   actor_type;
    u8    actor_tag[0x14];
    u8    draw_tag[0x14];
    void* sub_method;
    void* heap;
    u8    eventInfo[0x18];
    u8    tevStr[0x494 - 0x10C];
    u16   setID;
    u8    group;
    u8    cullType;
    u8    demoActorID;
    s8    argument;
    u8    carryType;
    u8    field_0x49b;
    u32   actor_status;
    u32   actor_condition;
    fpc_ProcID parentActorID;
    actor_place home;
    actor_place old;
    actor_place current;
    csXyz shape_angle;
    u8    field_0x4ea[2];
    cXyz  scale;
    cXyz  speed;
    void* cullMtx;
    u8    field_0x508[0x18];
    f32   cullSizeFar;
    void* model;
    void* jntCol;
    f32   speedF;
    f32   gravity;
    f32   maxFallSpeed;
    cXyz  eyePos;
    actor_attention_types attention_info;
    s16   field_0x564;
    s16   health;
};

TPHD_ASSERT_OFFSET(fopAc_ac_c, actor_type, 0x0C0);
TPHD_ASSERT_OFFSET(fopAc_ac_c, sub_method, 0x0EC);
TPHD_ASSERT_OFFSET(fopAc_ac_c, setID, 0x494);
TPHD_ASSERT_OFFSET(fopAc_ac_c, argument, 0x499);
TPHD_ASSERT_OFFSET(fopAc_ac_c, actor_status, 0x49C);
TPHD_ASSERT_OFFSET(fopAc_ac_c, parentActorID, 0x4A4);
TPHD_ASSERT_OFFSET(fopAc_ac_c, home, 0x4A8);
TPHD_ASSERT_OFFSET(fopAc_ac_c, old, 0x4BC);
TPHD_ASSERT_OFFSET(fopAc_ac_c, current, 0x4D0);
TPHD_ASSERT_OFFSET(fopAc_ac_c, shape_angle, 0x4E4);
TPHD_ASSERT_OFFSET(fopAc_ac_c, scale, 0x4EC);
TPHD_ASSERT_OFFSET(fopAc_ac_c, speed, 0x4F8);
TPHD_ASSERT_OFFSET(fopAc_ac_c, speedF, 0x52C);
TPHD_ASSERT_OFFSET(fopAc_ac_c, maxFallSpeed, 0x534);
TPHD_ASSERT_OFFSET(fopAc_ac_c, eyePos, 0x538);
TPHD_ASSERT_OFFSET(fopAc_ac_c, attention_info, 0x544);
TPHD_ASSERT_OFFSET(fopAc_ac_c, health, 0x566);
TPHD_ASSERT_SIZE(fopAc_ac_c, 0x568);

inline cXyz* fopAcM_GetPosition(fopAc_ac_c* ac) { return &ac->current.pos; }
inline cXyz* fopAcM_GetSpeed(fopAc_ac_c* ac) { return &ac->speed; }
inline s16 fopAcM_GetShapeAngleY(const fopAc_ac_c* ac) { return ac->shape_angle.y; }
inline s8 fopAcM_GetRoomNo(const fopAc_ac_c* ac) { return ac->current.roomNo; }
inline fpc_ProcID fopAcM_GetID(const fopAc_ac_c* ac) { return ac->id; }
inline s16 fopAcM_GetName(const fopAc_ac_c* ac) { return ac->name; }
