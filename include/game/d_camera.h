// game/d_camera.h -- camera transform, freeze/decouple flags, camera engine
// state, and live controller input, for the fly cam and modern camera.

#pragma once

#include "game/types.h"

#define GAME_ADDR_cameraPtr       0x1014B578u
// GAME_ADDR_cameraPtr holds camera_process_class*. Its embedded dCamera_c is
// at +0x248 (verified in TPHD's camera init at 0x028E1F08). There are three
// distinct copies of the view transform:
//   process +0x278  dCamera_c::mCenter/mEye (final collision-adjusted view)
//   process +0x2AC  dCamera_c::mViewCache center/eye (next desired view)
//   process +0x0D8  rendered lookat eye/center (note the reversed order)
// Writing only mViewCache leaves the final and rendered copies stale, which
// makes the camera appear to interpolate into a coordinate load.
#define GAME_CAMERA_BODY_OFF              0x248u
#define GAME_CAMERA_CURRENT_XFORM_OFF     0x278u
#define GAME_CAMERA_XFORM_OFF             0x2ACu
#define GAME_CAMERA_RENDER_EYE_OFF        0x0D8u
#define GAME_CAMERA_RENDER_CENTER_OFF     0x0E4u
#define GAME_ADDR_dCamera_reset            0x028FDF64u
#define GAME_ADDR_linkPosPtr      0x1017F640u
// These two "flags" are real engine state, identified via dusklight:
//   freezeFlag   = play.mEvent.mEventStatus (0x1014A795) -- dEvt_control_c's
//                  runCheck() byte; nonzero pauses the world like an event.
//   decoupleFlag = LSB of dEvent_manager_c::mCameraPlay (int @ play+0x4250 ->
//                  0x1014A970). Nonzero means "an event owns the camera": the
//                  camera skips its engine AND rejects mode changes
//                  (FUN_028d9e60 returns 0), but the commit that publishes
//                  at/eye -- and derives the control angle Link steers by --
//                  still runs. That is exactly what lets us write at/eye.
#define GAME_ADDR_freezeFlag      0x1014A795u
#define GAME_ADDR_decoupleFlag    0x1014A973u

// ---- camera engine state (dCamera_c) -----------------------------------------
// RE'd from Zelda.rpx dCamera_c init/SetStyle (FUN_028e1f08 / FUN_028fec50),
// layouts cross-checked against dusklight's dCamera_c / dCamParam_c:
//   base = *(void**)GAME_ADDR_cameraPtr
//   +0x1A4  int  current mode (row 0..10 into the camtype.dat type entry).
//                Committed by the mode control (FUN_028dae5c via FUN_028df72c)
//                immediately BEFORE the engine dispatch, so it never lags the
//                dispatched frame. 0 = normal follow; every other value is a
//                special state: 1 = LockOn button held with no target (the
//                recenter-behind-Link, chosen straight off the LockOn input
//                mapping), 2 = locked on a target, 4 = first person,
//                7 = charge, 8 = locked-on while riding, 10 = event.
//   +0x68C  int  current camera type (index into camtype.dat)
//   +0x694  int  the stage's default follow type (FieldS / DungeonS / ...)
//   +0xB40  ptr  current style record (0x78-byte camtype.dat style entries)
//   +0xB44  int  current style index
// Style record +0x4 (int) is the engine algorithm the camera dispatches on.
#define DCAM_OFF_MODE            0x1A4u
#define DCAM_OFF_TYPE            0x68Cu
#define DCAM_OFF_DEFAULT_TYPE    0x694u
#define DCAM_OFF_STYLE_PTR       0xB40u
#define DCAM_OFF_STYLE_INDEX     0xB44u
#define DCAM_STYLE_OFF_ALGORITHM 0x4u

// Engine algorithms, in dusklight's dCamera_c::engine_tbl order. CHASE is the
// standard third-person follow; SUBJECT is R3 first person.
enum {
    DCAM_ALG_LET = 0,
    DCAM_ALG_CHASE,
    DCAM_ALG_LOCKON,
    DCAM_ALG_TALKTO,
    DCAM_ALG_SUBJECT,
    DCAM_ALG_FIXED_POS,
    DCAM_ALG_FIXED_FRAME,
    DCAM_ALG_TOWER,
    DCAM_ALG_RIDE,
    DCAM_ALG_MANUAL,
    DCAM_ALG_EVENT,
    DCAM_ALG_HOOKSHOT,
};

// The camera engine dispatch table (dusklight's dCamera_c::engine_tbl):
// twenty 8-byte member-fn-pointer entries {s16 thisDelta=0, s16 vtbl=-1,
// u32 func} starting at 0x1011CF2C. The dispatcher (0x028E3C2C..48, LR of
// the crash that pinned this alignment down) computes entry = base + alg*8,
// takes the non-virtual path when the s16 at entry+6... is -1, loads the
// function from entry+4 and bctrl's -- so ONLY the function word may be
// patched; the {0,-1} header must stay intact or the dispatcher walks the
// virtual-call path into garbage. Entry order confirmed against dusklight
// (entry 7 = 0x028F2E3C writes the 'TOWR' work tag = towerCamera, which
// falls back to entry 1's chaseCamera = 0x028E417C). Patching entry 1's
// function word hooks every standard-follow camera frame -- the same
// data-slot mechanism as the dScnPly phase_1 hook. The engines run one per
// frame, so first person / lock-on / ride / event cameras never enter a
// chase hook at all.
#define GAME_ADDR_camEngineTbl        0x1011CF2Cu
#define GAME_ADDR_camEngineChaseSlot  0x1011CF38u   // entry 1 func word (+4)
#define GAME_ADDR_dCamera_chaseCamera 0x028E417Cu
#define DCAM_ENGINE_COUNT             20
#define DCAM_ENGINE_ENTRY_STRIDE      8u
typedef int (*dCamEngine_t)(void* camera, int style);

// Function word of engine-table entry `index` (the only patchable word; the
// {0, -1} entry header must stay intact -- see above).
static inline volatile dCamEngine_t* dCam_engineSlot(int index)
{
    u32 addr = GAME_ADDR_camEngineTbl + (u32)index * DCAM_ENGINE_ENTRY_STRIDE + 4u;
    return (volatile dCamEngine_t*)addr;
}

// mViewCache (dCamera_c+0x5C; GC layout verified intact in TPHD -- init also
// writes mIsWolf at +0x190 and mCurMode at +0x1A4 exactly like dusklight).
// The direction is a cSGlobe {f32 R; s16 inclination; s16 azimuth} pointing
// from the center (look-at) to the eye:
//   eye = center + R * (cos(incl)*sin(az), sin(incl), cos(incl)*cos(az))
// with s16 angles (0x10000 = 360 degrees). fovy is in degrees.
#define DCAM_OFF_VIEW_R        0x5Cu
#define DCAM_OFF_VIEW_INCL     0x60u
#define DCAM_OFF_VIEW_AZIMUTH  0x62u
#define DCAM_OFF_VIEW_CENTER   0x64u
#define DCAM_OFF_VIEW_EYE      0x70u
#define DCAM_OFF_VIEW_FOVY     0x80u

// mPadInfo (dCamera_c+0x1BC): the camera's own per-frame pad cache, filled
// before the engine dispatch. Two 0x1C-byte sticks: main @+0x1BC, C-stick
// (the right stick on Wii U) @+0x1D8 {f32 lastX, f32 lastY, f32 magnitude,
// f32 dX, f32 dY, f32, s16 angle}. TPHD's chase engine reads the C-stick
// directly (e.g. lastX @ cam+0x1D8) for its HD-added native camera control.
// Immediately after it sits the C-stick Y state machine {int mCStickYState
// @+0x1F4, int mCStickYHoldCount @+0x1F8, int mCStickUpLatch @+0x1FC}, also
// recomputed from the raw stick by the pad fill each frame; chase tests
// mCStickYHoldCount == 1 to step the zoom gear (mGear @ cam+0x940), which is
// TPHD's native stick-up/down zoom. Blinding the ENGINE to the stick means
// zeroing this whole +0x1D8..+0x1FF window at dispatch time -- the fill and
// the mode/attention logic already consumed the real values before the
// dispatch, so R3/first-person handling is unaffected, and the fill rewrites
// the window next frame.
#define DCAM_OFF_PAD_CSTICK       0x1D8u
#define DCAM_OFF_PAD_CSTICK_X     0x1D8u
#define DCAM_OFF_PAD_CSTICK_Y     0x1DCu
#define DCAM_PAD_STICK_SIZE       0x1Cu
#define DCAM_PAD_CSTICK_WINDOW    0x28u   // stick block + Y state machine

// mGear (dCamera_c+0x940): the stick-zoom step the HD camera keeps (chase
// and tower substitute closer style params while it's 1, and chase starts a
// zoom transition when it changes).
#define DCAM_OFF_GEAR             0x940u

// play.mPlayerStatus[padId][0] @ 0x1014B728 (4 words per player = stride
// 0x10, indexed by the camera's pad id @ dCamera_c+0x17C). This is the array
// the camera MODE CONTROL (FUN_028dae5c) polls as
// (&DAT_1014b728)[padId*4] -- it is the PLAYER STATUS word (GC
// dComIfGp_checkPlayerStatus0 / check_owner_action), NOT the per-camera
// attention status. Bit 0x200000 is "Z-target engaged": daAlink toggles it on
// the LockOn button (FUN_0203b5f8, with the lock engage/disengage sounds),
// and the mode control keys EVERY lock-on decision off it -- lock camera
// when a target is held, recenter-behind-Link when there is none. While it
// is set the follow camera must behave vanilla.
#define GAME_ADDR_playerStatus0      0x1014B728u
#define DPLY_STATUS_STRIDE           0x10u
#define DPLY_STATUS0_ZTARGET         0x200000u
#define DCAM_OFF_PAD_ID              0x17Cu

// NOTE: bit 0x200000 is NOT the full lock-on signal -- the mode control only
// consults it on the horseback/charge variants and picks the regular lock-on
// and no-target-recenter modes straight off the LockOn input mapping. Gate
// vanilla-camera passthrough on dCam_getMode() != 0 instead.
static inline bool dCam_isZTargetEngaged(const void* camera)
{
    u32 padId = *(const volatile u32*)((const u8*)camera + DCAM_OFF_PAD_ID);
    if (padId > 3)
        return false;
    u32 status = *(volatile u32*)(GAME_ADDR_playerStatus0 +
                                  padId * DPLY_STATUS_STRIDE);
    return (status & DPLY_STATUS0_ZTARGET) != 0;
}

// Camera modes (the values committed to DCAM_OFF_MODE; see the offset table
// above for how the mode control derives them).
enum {
    DCAM_MODE_FOLLOW    = 0,   // normal follow
    DCAM_MODE_RESET     = 1,   // LockOn held, no target: recenter behind Link
    DCAM_MODE_LOCK      = 2,   // locked on a target
    DCAM_MODE_SUBJECT   = 4,   // first person
    DCAM_MODE_CHARGE    = 7,   // charge/spin
    DCAM_MODE_RIDE_LOCK = 8,   // locked on a target while riding
    DCAM_MODE_EVENT     = 10,  // event camera
};

// Current camera mode (see DCAM_OFF_MODE). Fresh at dispatch time.
static inline int dCam_getMode(const void* camera)
{
    return *(const volatile s32*)((const u8*)camera + DCAM_OFF_MODE);
}

// Current engine algorithm, or -1 while the camera/style isn't set up yet.
static inline int dCam_getAlgorithm(void)
{
    u32 cam = *(volatile u32*)GAME_ADDR_cameraPtr;
    if (!cam)
        return -1;
    u32 style = *(volatile u32*)(cam + DCAM_OFF_STYLE_PTR);
    if (!style)
        return -1;
    return *(volatile int*)(style + DCAM_STYLE_OFF_ALGORITHM);
}

static inline int dCam_getStyleIndex(void)
{
    u32 cam = *(volatile u32*)GAME_ADDR_cameraPtr;
    return cam ? *(volatile int*)(cam + DCAM_OFF_STYLE_INDEX) : -1;
}
#define GAME_ADDR_mirrorFlag      0x1012667Du
#define GAME_ADDR_controller      0x1012D554u
#define GAME_ADDR_controllerType  0x1012D547u

// Pro-controller button codes
#define PRO_BTN_A    0x00000010u
#define PRO_BTN_B    0x00000040u
#define PRO_BTN_X    0x00000008u
#define PRO_BTN_Y    0x00000020u
#define PRO_BTN_ZR   0x00000004u
#define PRO_BTN_ZL   0x00000080u
#define PRO_BTN_R    0x00000200u
#define PRO_BTN_L    0x00002000u
#define PRO_BTN_R3   0x00010000u
#define PRO_BTN_L3   0x00020000u
#define PRO_COMBO    0x00030084u   // ZL | ZR | L3 | R3

// Camera target/eye pair. dCam_getXform() is the controller's desired cache,
// which fly cam intentionally owns while the normal camera engine is disabled.
typedef struct CameraXform {
    cXyz at;    // 0x00  ("target")
    cXyz eye;   // 0x0C  ("pos")
} CameraXform;

static inline CameraXform* dCam_getXform(void)
{
    u32 p = *(volatile u32*)GAME_ADDR_cameraPtr;
    if (!p)
        return (CameraXform*)0;
    return (CameraXform*)(p + GAME_CAMERA_XFORM_OFF);
}

// Final collision-adjusted target/eye used by the normal camera. This is the
// right view to capture for coordinate saves; it may differ from mViewCache.
static inline CameraXform* dCam_getCurrentXform(void)
{
    u32 p = *(volatile u32*)GAME_ADDR_cameraPtr;
    if (!p)
        return (CameraXform*)0;
    return (CameraXform*)(p + GAME_CAMERA_CURRENT_XFORM_OFF);
}

typedef int (*dCameraReset_t)(void* camera);

// Snap all camera layers to one transform through the game's own Reset().
// TPHD 0x028FDF64 copies mCenter/mEye to mViewCache, rebuilds direction and
// controlled yaw, and clears its HD transition scalar. Refresh the outer
// rendered look-at pair too so no already-published view is left behind.
static inline bool dCam_snapXform(const cXyz* at, const cXyz* eye)
{
    u32 process = *(volatile u32*)GAME_ADDR_cameraPtr;
    if (!process || !at || !eye)
        return false;

    CameraXform* current =
        (CameraXform*)(process + GAME_CAMERA_CURRENT_XFORM_OFF);
    current->at = *at;
    current->eye = *eye;

    dCameraReset_t reset = (dCameraReset_t)GAME_ADDR_dCamera_reset;
    reset((void*)(process + GAME_CAMERA_BODY_OFF));

    *(cXyz*)(process + GAME_CAMERA_RENDER_EYE_OFF) = *eye;
    *(cXyz*)(process + GAME_CAMERA_RENDER_CENTER_OFF) = *at;
    return true;
}

static inline cXyz* dCam_getLinkPos(void)
{
    u32 p = *(volatile u32*)GAME_ADDR_linkPosPtr;
    if (!p)
        return (cXyz*)0;
    return (cXyz*)p;
}

static inline void dCam_setFreeze(bool on)   { *(volatile u8*)GAME_ADDR_freezeFlag   = on ? 1 : 0; }
static inline bool dCam_getFreeze(void)      { return *(volatile u8*)GAME_ADDR_freezeFlag != 0; }
static inline void dCam_setDecouple(bool on) { *(volatile u8*)GAME_ADDR_decoupleFlag = on ? 1 : 0; }
static inline bool dCam_getMirror(void)      { return *(volatile u8*)GAME_ADDR_mirrorFlag == 1; }

// Live game controller input: raw device buttons + analog sticks (-1..1).
typedef struct GameInput {
    u32   buttons;
    float leftX, leftY, rightX, rightY;
    u8    type;     // 0 = GamePad, 1 = Pro Controller
} GameInput;

// Resolves the active controller's input block
//   GamePad: (*(*_controller + 4)) + 0x0C
//   Pro:     (*((*(*_controller + 4)) + 0x10)) + 0x70
// buttons @ +0x00, sticks @ +0x0C/+0x10/+0x14/+0x18.
static inline bool dCam_getInput(GameInput* o)
{
    u32 ctrl = *(volatile u32*)GAME_ADDR_controller;
    if (!ctrl)
        return false;
    u32 p = *(volatile u32*)(ctrl + 0x04);
    if (!p)
        return false;
    u8  type = *(volatile u8*)GAME_ADDR_controllerType;
    u32 base;
    if (type == 1) {
        u32 q = *(volatile u32*)(p + 0x10);
        if (!q)
            return false;
        base = q + 0x70;
    } else {
        base = p + 0x0C;
    }
    o->buttons = *(volatile u32*)  (base + 0x00);
    o->leftX   = *(volatile float*)(base + 0x0C);
    o->leftY   = *(volatile float*)(base + 0x10);
    o->rightX  = *(volatile float*)(base + 0x14);
    o->rightY  = *(volatile float*)(base + 0x18);
    o->type    = type;
    return true;
}

// Normalize raw device buttons to Pro-controller codes (GamePad gets remapped;
// Pro passes through)
static inline u32 dCam_normalizeButtons(u32 g, u8 type)
{
    if (type == 1)
        return g;
    u32 p = 0;
    if (g & 0x00008000) p |= 0x00000010;  // A
    if (g & 0x00004000) p |= 0x00000040;  // B
    if (g & 0x00002000) p |= 0x00000008;  // X
    if (g & 0x00001000) p |= 0x00000020;  // Y
    if (g & 0x00000008) p |= 0x00000400;  // +
    if (g & 0x00000004) p |= 0x00001000;  // -
    if (g & 0x00040000) p |= 0x00020000;  // L3
    if (g & 0x00020000) p |= 0x00010000;  // R3
    if (g & 0x00000020) p |= 0x00002000;  // L
    if (g & 0x00000080) p |= 0x00000080;  // ZL
    if (g & 0x00000010) p |= 0x00000200;  // R
    if (g & 0x00000040) p |= 0x00000004;  // ZR
    if (g & 0x00000200) p |= 0x00000001;  // D-Up
    if (g & 0x00000100) p |= 0x00004000;  // D-Down
    if (g & 0x00000800) p |= 0x00000002;  // D-Left
    if (g & 0x00000400) p |= 0x00008000;  // D-Right
    return p;
}
