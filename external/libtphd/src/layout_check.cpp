#include "libtphd/addr_v81.h"
#include "libtphd/assert.h"
#include "libtphd/reloc.h"
#include "libtphd/types.h"

#include "SSystem/SComponent/c_angle.h"
#include "SSystem/SComponent/c_counter.h"
#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "Z2AudioLib/Z2StatusMgr.h"
#include "c/c_damagereaction.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_horse.h"
#include "d/actor/d_a_obj_amiShutter.h"
#include "d/actor/d_a_player.h"
#include "d/d_cam_param.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "d/d_file_select.h"
#include "d/d_item.h"
#include "d/d_procname.h"
#include "d/d_save.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_scene_req.h"
#include "f_pc/f_pc_base.h"
#include "f_pc/f_pc_leaf.h"
#include "m_Do/m_Do_Reset.h"
#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_graphic.h"

TPHD_ASSERT_SIZE(cXyz, 0x0C);
TPHD_ASSERT_SIZE(csXyz, 0x06);

static_assert(__is_trivially_copyable(cXyz), "cXyz must stay trivially copyable");
static_assert(__is_trivially_copyable(csXyz), "csXyz must stay trivially copyable");
static_assert(__is_trivially_copyable(fopAc_ac_c), "fopAc_ac_c must stay trivially copyable");
static_assert(__is_trivially_copyable(dSv_info_c), "dSv_info_c must stay trivially copyable");
