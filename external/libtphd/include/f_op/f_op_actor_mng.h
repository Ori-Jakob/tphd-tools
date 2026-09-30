#pragma once

#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor.h"
#include "f_pc/f_pc_base.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct fopAcM_prm_class {
    u32   dwParameters;
    cXyz  position;
    csXyz angle;
    u16   wSetID;
    u8    pScale[3];
    u8    field_0x1b;
    u32   dwParent_id;
    s8    cArgument;
    s8    cRoom_no;
    u8    field_0x22[2];
};

TPHD_ASSERT_OFFSET(fopAcM_prm_class, wSetID, 0x16);
TPHD_ASSERT_OFFSET(fopAcM_prm_class, cArgument, 0x20);
TPHD_ASSERT_SIZE(fopAcM_prm_class, 0x24);

#define fopAcM_SETID_DYNAMIC 0xFFFFu

typedef fopAcM_prm_class* (*fopAcM_CreateAppend_t)(void);
#define fopAcM_CreateAppend ((fopAcM_CreateAppend_t)TPHD_ADDR_fopAcM_CreateAppend)

typedef void* (*fpcLy_CurrentLayer_t)(void);
#define fpcLy_CurrentLayer ((fpcLy_CurrentLayer_t)TPHD_ADDR_fpcLy_CurrentLayer)

typedef fpc_ProcID (*fpcSCtRq_Request_t)(void* layer, int procName, void* a,
                                         void* b, fopAcM_prm_class* prm);
#define fpcSCtRq_Request ((fpcSCtRq_Request_t)TPHD_ADDR_fpcSCtRq_Request)

typedef s32 (*fopAcM_SearchByName_t)(s16 procName, fopAc_ac_c** outActor);
#define fopAcM_SearchByName ((fopAcM_SearchByName_t)TPHD_ADDR_fopAcM_SearchByName)

typedef fopAc_ac_c* (*fopAcIt_JudgeFunc_t)(fopAc_ac_c* actor, void* context);
typedef fopAc_ac_c* (*fopAcIt_Judge_t)(fopAcIt_JudgeFunc_t callback, void* context);
#define fopAcIt_Judge ((fopAcIt_Judge_t)TPHD_ADDR_fopAcIt_Judge)

typedef void* (*fpcM_JudgeFunc_t)(void* process, void* context);
typedef void* (*fpcM_Judge_t)(fpcM_JudgeFunc_t callback, void* context);
#define fpcM_Judge ((fpcM_Judge_t)TPHD_ADDR_fpcM_Judge)

typedef s32 (*fpcM_Delete_t)(void* process);
#define fpcM_Delete ((fpcM_Delete_t)TPHD_ADDR_fpcM_Delete)

inline fopAc_ac_c* fopAcM_searchByName(s16 procName)
{
    fopAc_ac_c* actor = nullptr;
    if (!fopAcM_SearchByName(procName, &actor))
        return nullptr;
    return actor;
}

inline void* fpcM_findNameJudge(void* process, void* context)
{
    s16 wanted = *(const s16*)context;
    return process && ((base_process_class*)process)->name == wanted ? process
                                                                     : nullptr;
}

inline void* fpcM_searchProcessByName(s16 procName)
{
    return fpcM_Judge(fpcM_findNameJudge, &procName);
}

inline bool fpcM_isProcessPresent(s16 procName)
{
    return fpcM_searchProcessByName(procName) != nullptr;
}

inline bool fopAcM_create(s16 procName, u32 params, s8 subtype, s8 roomNo,
                          const cXyz* pos, const csXyz* angle)
{
    fopAcM_prm_class* prm = fopAcM_CreateAppend();
    if (!prm)
        return false;
    prm->dwParameters = params;
    prm->cArgument = subtype;
    prm->cRoom_no = roomNo;
    if (pos)
        prm->position = *pos;
    if (angle)
        prm->angle = *angle;
    return fpcSCtRq_Request(fpcLy_CurrentLayer(), procName, nullptr, nullptr, prm) !=
           fpcM_ERROR_PROCESS_ID;
}

inline bool fopAcM_createAtLink(s16 procName, u32 params, s8 subtype)
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!link)
        return false;
    return fopAcM_create(procName, params, subtype, link->current.roomNo,
                         &link->current.pos, &link->current.angle);
}
