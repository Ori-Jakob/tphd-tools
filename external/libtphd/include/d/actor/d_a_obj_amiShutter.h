#pragma once

#include "d/d_procname.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct daAmiShutter_c : fopAc_ac_c {
    u8  field_0x0568[0x05C8 - 0x0568];
    u8  mMode;
    u8  mType;
    u8  field_0x05ca[2];
    f32 mClosedPosZ;
    u8  field_0x05d0[0x05DD - 0x05D0];
    u8  mOpen;
};

TPHD_ASSERT_OFFSET(daAmiShutter_c, mMode, 0x05C8);
TPHD_ASSERT_OFFSET(daAmiShutter_c, mClosedPosZ, 0x05CC);
TPHD_ASSERT_OFFSET(daAmiShutter_c, mOpen, 0x05DD);

#define daAmiShutter_TYPE_EVENT     0u
#define daAmiShutter_MODE_CLOSE_END 4u

typedef void (*daAmiShutter_setBaseMtx_t)(daAmiShutter_c* shutter);
#define daAmiShutter_setBaseMtx \
    ((daAmiShutter_setBaseMtx_t)TPHD_ADDR_daAmiShutter_setBaseMtx)

inline bool daAmiShutter_setClosed(daAmiShutter_c* shutter)
{
    if (!shutter || fopAcM_GetName(shutter) != fpcNm_d_a_obj_amiShutter)
        return false;
    if (shutter->mType != daAmiShutter_TYPE_EVENT)
        return false;

    shutter->current.pos.z = shutter->mClosedPosZ;
    shutter->mMode = daAmiShutter_MODE_CLOSE_END;
    shutter->mOpen = 0;
    daAmiShutter_setBaseMtx(shutter);
    return true;
}
