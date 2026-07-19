// game/d_actor.h -- spawn actors at runtime.
//
// Port of tpgz's actorFastCreateAtLink (modules/menus/menu_actor_spawn): build a
// creation request (fopAcM_prm_class), then queue it on the current layer. This
// is the engine's own dynamic-spawn path -- the same one dStage_actorCreate uses
// for stage actors -- so spawned actors behave like normal placed ones.
//
// Engine bindings are TPHD Zelda.rpx (US), confirmed in Ghidra:
//   fopAcM_CreateAppend  @ 0x02ace4ac  (allocs+inits a 0x22-byte prm: wSetID=0xFFFF,
//                                       scale=10/10/10, cArgument/cRoom_no=-1)
//   fpcLy_CurrentLayer   @ 0x02ad7748  (returns the current layer, *(0x1012a1b8))
//   fpcSCtRq_Request     @ 0x02ad9b9c  (queue creation: (layer, procName, 0, 0, prm);
//                                       returns the new proc id, or 0xFFFFFFFF on fail)
// The call sequence was lifted from dStage_actorCreate @ 0x02ab6bcc:
//   layer = fpcLy_CurrentLayer(); fpcSCtRq_Request(layer, procName, 0, 0, prm);
#pragma once

#include "game/types.h"
#include "game/f_op_actor.h"
#include "game/d_com_inf_game.h"

// fopAcM_prm_class -- the actor creation request (Ghidra struct, 0x22 bytes).
typedef struct fopAcM_prm_class {
    /* 0x00 */ u32   dwParameters;   // actor params (per-actor meaning)
    /* 0x04 */ cXyz  position;       // world position
    /* 0x10 */ csXyz angle;          // spawn facing (.y = yaw)
    /* 0x16 */ u16   wSetID;         // placement id; 0xFFFF = dynamic spawn
    /* 0x18 */ u8    scale[3];       // bX / bY / bZ (10 = 1.0)
    /* 0x1B */ u8    field_0x1b;
    /* 0x1C */ u32   dwParent_id;    // 0xFFFFFFFF = no parent
    /* 0x20 */ s8    cArgument;      // subtype / "switch"
    /* 0x21 */ s8    cRoom_no;       // room the actor belongs to
} fopAcM_prm_class;

typedef fopAcM_prm_class* (*fopAcM_CreateAppend_t)(void);
#define fopAcM_CreateAppend ((fopAcM_CreateAppend_t)0x02ace4acu)

typedef void* (*fpcLy_CurrentLayer_t)(void);
#define fpcLy_CurrentLayer ((fpcLy_CurrentLayer_t)0x02ad7748u)

// (layer, procName/actor id, unused, unused, prm) -> new proc id (0xFFFFFFFF = fail).
typedef u32 (*fpcSCtRq_Request_t)(void* layer, int procName, void* a, void* b,
                                  fopAcM_prm_class* prm);
#define fpcSCtRq_Request ((fpcSCtRq_Request_t)0x02ad9b9cu)

#define DACTOR_SETID_DYNAMIC 0xFFFFu
#define DACTOR_SPAWN_FAILED  0xFFFFFFFFu
// Proc ids verified against the TPHD runtime proc-name table (the {u16 id;
// char* name} array @ 0x100034a0 that FUN_020096e0 loads into DAT_10131250).
// The earlier values were vanilla-TP ids and were off by one here: TPHD 0x02E1
// is d_a_movie_player and 0x00FD is d_a_tbox2.
#define FPCNM_TITLE          0x02E2  // d_a_title
#define FPCNM_E_DT           0x0201  // d_a_e_dt (Deku Toad)
#define FPCNM_OBJ_AMISHUTTER 0x0048  // d_a_obj_amiShutter (Deku Toad grate)
#define FPCNM_NBOMB          0x0222  // d_a_nbomb (normal/water/bombling actor)
// Link (d_a_player/d_a_alink) is NOT a name-loaded profile, so it has no entry
// in that table. This value is a placeholder; prefer guarding Link by pointer
// identity (compare against dComIfGp_getPlayer()) rather than by proc id.
#define FPCNM_LINK           0x00FD

// fopAcM_SearchByName(procName, &outActor) -- TPHD equivalent of Dusklight's
// fopAcM_SearchByName. It searches the live actor process list by fpcNm_* id and
// clears outActor if the match is still in creation.
typedef s32 (*fopAcM_SearchByName_t)(s16 procName, fopAc_ac_c** outActor);
#define fopAcM_SearchByName ((fopAcM_SearchByName_t)0x02ace424u)

static inline fopAc_ac_c* fopAcM_searchByName(s16 procName)
{
    fopAc_ac_c* actor = nullptr;
    if (!fopAcM_SearchByName(procName, &actor))
        return nullptr;
    return actor;
}

static inline bool dTitle_isTitleActorLoaded(void)
{
    return fopAcM_searchByName(FPCNM_TITLE) != nullptr;
}

// Walk the live actor list. The callback receives an actor and caller-owned
// context, and returns a non-null actor to stop the walk or nullptr to continue.
typedef fopAc_ac_c* (*fopAcIt_JudgeFunc_t)(fopAc_ac_c* actor, void* context);
typedef fopAc_ac_c* (*fopAcIt_Judge_t)(fopAcIt_JudgeFunc_t callback, void* context);
#define fopAcIt_Judge ((fopAcIt_Judge_t)0x02ace114u)

// Generic process-delete request. Destruction is asynchronous: callers must not
// mutate the actor queue while walking it.
typedef s32 (*fpcM_Delete_t)(void* process);
#define fpcM_Delete ((fpcM_Delete_t)0x02ad7078u)

// fpc_process_node fields verified in TPHD. Every actor begins with this base.
static inline u32 fopAcM_GetID(const fopAc_ac_c* actor)
{
    return *(const volatile u32*)((const u8*)actor + 0x04);
}

static inline s16 fopAcM_GetName(const fopAc_ac_c* actor)
{
    return *(const volatile s16*)((const u8*)actor + 0x08);
}

// Spawn actor `procName` (an fpcNm_ actor proc id) at pos/room with the given
// params/subtype/facing. pos or angle may be null to leave the prm defaults.
// Returns false if the request couldn't be queued (bad id / pool full).
static inline bool dActor_spawn(s16 procName, u32 params, s8 subtype, s8 roomNo,
                                const cXyz* pos, const csXyz* angle)
{
    fopAcM_prm_class* prm = fopAcM_CreateAppend();
    if (!prm)
        return false;
    prm->dwParameters = params;
    prm->cArgument    = subtype;
    prm->cRoom_no     = roomNo;
    if (pos)
        prm->position = *pos;
    if (angle)
        prm->angle = *angle;
    void* layer = fpcLy_CurrentLayer();
    return fpcSCtRq_Request(layer, procName, 0, 0, prm) != DACTOR_SPAWN_FAILED;
}

// Convenience: spawn at Link's exact position / room / facing (tpgz's
// actorFastCreateAtLink). Returns false if Link isn't loaded or the queue failed.
static inline bool dActor_spawnAtLink(s16 procName, u32 params, s8 subtype)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return false;
    return dActor_spawn(procName, params, subtype, link->current.roomNo,
                        &link->current.pos, &link->current.angle);
}
