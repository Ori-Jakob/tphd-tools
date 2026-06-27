// game/d_camera.h -- camera transform, freeze/decouple flags, and live
// controller input, for the fly cam.

#pragma once

#include "game/types.h"

#define GAME_ADDR_cameraPtr       0x1014B578u
#define GAME_CAMERA_XFORM_OFF     0x2ACu
#define GAME_ADDR_linkPosPtr      0x1017F640u
#define GAME_ADDR_freezeFlag      0x1014A795u
#define GAME_ADDR_decoupleFlag    0x1014A973u
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

// Camera at/eye (dCamera_c center/eye) the decoupled camera renders from.
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
