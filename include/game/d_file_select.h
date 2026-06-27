// game/d_file_select.h -- title-screen file-select and scene bindings.
//
// TPHD's title/file menu is owned by the Name Scene (process 0x0D). The scene
// stores its live dFile_select_c pointer at +0x414. dFile_select_c dispatches
// through a state byte at +0x2D2; the new-file confirmation path commits the
// selected Hero Mode and changes that state to 0x39 in FUN_02940fd4.
//
// Zelda.rpx (US v81), confirmed in Ghidra:
//   all-layer process iterator     @ 0x02AD7A80
//   dScnName_c file-select owner   @ +0x414 (FUN_02ac0018/FUN_02ac04c0)
//   dFile_select_c selected slot   @ +0x2C6
//   dFile_select_c Hero choice     @ +0x2CA
//   dFile_select_c selection path  @ +0x2D1 (4 = new-file name/difficulty path)
//   dFile_select_c state           @ +0x2D2 (0x39 = new file committed)
//   dFile_select_c load result     @ +0x2D3 (1 = success, 2 = failure)
#pragma once

#include "game/types.h"

// Scene process names used by dScnPly's scene-change path.
#define FPCNM_SCENE_PLAY     0x000B
#define FPCNM_SCENE_OPENING  0x000C
#define FPCNM_SCENE_NAME     0x000D
#define FPCNM_SCENE_LOADING  0x000F

// FUN_02ad7a80 walks every process list on every live layer. This is the same
// engine path used by FUN_02ad72b0 to resolve a process ID. It is broader than
// fopAcM_SearchByName, which only walks the actor queue.
typedef void* (*fpcM_JudgeFunc_t)(void* process, void* context);
typedef void* (*fpcM_Judge_t)(fpcM_JudgeFunc_t callback, void* context);
#define fpcM_Judge ((fpcM_Judge_t)0x02AD7A80u)

static inline void* fpcM_findNameJudge(void* process, void* context)
{
    s16 wanted = *(const s16*)context;
    return process && *(volatile const s16*)((const u8*)process + 0x08) == wanted
               ? process
               : nullptr;
}

static inline void* fpcM_searchProcessByName(s16 procName)
{
    return fpcM_Judge(fpcM_findNameJudge, &procName);
}

static inline bool dScn_isPresent(s16 procName)
{
    return fpcM_searchProcessByName(procName) != nullptr;
}

enum dFile_select_path {
    DFILE_SELECT_PATH_NEW_FILE = 4,
};

enum dFile_select_state {
    // FUN_02940fd4 copies/initializes the new save image, commits Hero Mode,
    // and stores this state. State 0x39 dispatches FUN_02948138 while the card
    // operation finishes.
    DFILE_SELECT_STATE_NEW_FILE_COMMITTED = 0x39,
};

enum dFile_select_result {
    DFILE_SELECT_RESULT_PENDING = 0,
    DFILE_SELECT_RESULT_SUCCESS = 1,
    DFILE_SELECT_RESULT_FAILURE = 2,
};

typedef struct dFile_select_c {
    u8 _pad0x000[0x2B8];
    u8 mSlotState[3];       // 0x2B8: one byte per file slot
    u8 mSlotError[3];       // 0x2BB: one byte per file slot
    u8 _pad0x2BE[0x2C6 - 0x2BE];
    u8 mSelectedSlot;       // 0x2C6
    u8 _pad0x2C7[0x2CA - 0x2C7];
    u8 mHeroModeChoice;     // 0x2CA: UI choice; committed as inverse 0/1 flag
    u8 mPreviousHeroChoice; // 0x2CB
    u8 _pad0x2CC[0x2D1 - 0x2CC];
    u8 mSelectionPath;      // 0x2D1: 4 on the new-file name/difficulty path
    u8 mState;              // 0x2D2: state-machine dispatch index
    u8 mLoadResult;         // 0x2D3: dFile_select_result
} dFile_select_c;

typedef struct dScnName_c {
    u8 _pad0x000[0x414];
    dFile_select_c* mFileSelect; // 0x414
} dScnName_c;

static inline dScnName_c* dScnName_get(void)
{
    return (dScnName_c*)fpcM_searchProcessByName(FPCNM_SCENE_NAME);
}

static inline dFile_select_c* dScnName_getFileSelect(void)
{
    dScnName_c* scene = dScnName_get();
    return scene ? scene->mFileSelect : nullptr;
}

#ifdef __cplusplus
#include <cstddef>
static_assert(offsetof(dFile_select_c, mSelectedSlot) == 0x2C6,
              "dFile_select_c.mSelectedSlot");
static_assert(offsetof(dFile_select_c, mHeroModeChoice) == 0x2CA,
              "dFile_select_c.mHeroModeChoice");
static_assert(offsetof(dFile_select_c, mSelectionPath) == 0x2D1,
              "dFile_select_c.mSelectionPath");
static_assert(offsetof(dFile_select_c, mState) == 0x2D2,
              "dFile_select_c.mState");
static_assert(offsetof(dFile_select_c, mLoadResult) == 0x2D3,
              "dFile_select_c.mLoadResult");
static_assert(offsetof(dScnName_c, mFileSelect) == 0x414,
              "dScnName_c.mFileSelect");
#endif
