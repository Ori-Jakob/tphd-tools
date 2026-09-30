#pragma once

#include "SSystem/SComponent/c_xyz.h"
#include "libtphd/assert.h"
#include "libtphd/types.h"

struct dSv_player_status_a_c {
    u8  field_0x00[2];
    u16 mMaxLife;
    u16 mLife;
    u16 mRupee;
    u16 mMaxOil;
    u16 mOil;
    u8  field_0x0c;
    u8  mSelectItem[4];
    u8  mMixItem[4];
    u8  mSelectEquip[6];
    u8  mWalletSize;
    u8  mMaxMagic;
    u8  mMagic;
    u8  mMagicFlag;
    u8  field_0x1f;
    u8  mTransformStatus;
    u8  field_0x21;
};

TPHD_ASSERT_OFFSET(dSv_player_status_a_c, mLife, 0x04);
TPHD_ASSERT_OFFSET(dSv_player_status_a_c, mWalletSize, 0x1B);
TPHD_ASSERT_SIZE(dSv_player_status_a_c, 0x22);

enum {
    dSv_EQUIP_ARMOR = 0,
    dSv_EQUIP_SWORD,
    dSv_EQUIP_SHIELD,
    dSv_EQUIP_SCENT,
};

struct dSv_player_return_place_c {
    char mName[8];
    u8   mPlayerStatus;
    s8   mRoomNo;
    u8   field_0x0a[2];
};

TPHD_ASSERT_SIZE(dSv_player_return_place_c, 0x0C);

struct dSv_player_field_last_stay_info_c {
    cXyz mPos;
    s16  mAngleY;
    char mName[8];
    s8   mRoomNo;
    u8   mRegionNo;
    u8   mExistFlag;
    u8   field_0x19[3];
};

TPHD_ASSERT_OFFSET(dSv_player_field_last_stay_info_c, mName, 0x0E);
TPHD_ASSERT_OFFSET(dSv_player_field_last_stay_info_c, mRoomNo, 0x16);

struct dSv_player_item_c {
    u8 mItems[24];
    u8 mItemSlots[24];
};

TPHD_ASSERT_SIZE(dSv_player_item_c, 0x30);

struct dSv_player_get_item_c {
    u32 mItemFlags[8];
};

struct dSv_player_item_record_c {
    u8 mArrowNum;
    u8 mBombNum[3];
    u8 mBottleNum[4];
    u8 mPachinkoNum;
};

struct dSv_player_item_max_c {
    u8 mItemMax[8];
};

enum {
    dSv_MAX_ARROW = 0,
    dSv_MAX_NORMAL_BOMB = 1,
    dSv_MAX_WATER_BOMB = 2,
    dSv_MAX_POKE_BOMB = 6,
};

#define dSv_BOMB_SLOT_FIRST  15
#define dSv_MAX_PACHINKO     50u

struct dSv_memBit_c {
    u32 mTbox[2];
    u32 mSwitch[4];
    u32 mItem[1];
    u8  mKeyNum;
    u8  mDungeonItem;
    u8  field_0x1e[2];
};

TPHD_ASSERT_OFFSET(dSv_memBit_c, mDungeonItem, 0x1D);
TPHD_ASSERT_SIZE(dSv_memBit_c, 0x20);

enum {
    dSv_MEMBIT_MAP = 0,
    dSv_MEMBIT_COMPASS,
    dSv_MEMBIT_BOSS_KEY,
    dSv_MEMBIT_BOSS_DEFEATED,
    dSv_MEMBIT_HEART_CONTAINER,
    dSv_MEMBIT_BOSS_DEMO,
    dSv_MEMBIT_OOCCOO,
    dSv_MEMBIT_MINIBOSS,
};

struct dSv_event_c {
    u8 mEvent[0x100];
};

TPHD_ASSERT_SIZE(dSv_event_c, 0x100);

struct dSv_zone_c {
    s8 mRoomNo;
    u8 field_0x01;
    u8 mFlags[0x0C];
    u8 field_0x0e[2];
    u8 field_0x10[0x10];
};

TPHD_ASSERT_SIZE(dSv_zone_c, 0x20);

struct dSv_restart_c {
    s8   mRoomNo;
    u8   field_0x01[3];
    s16  mStartPoint;
    s16  mRoomAngleY;
    cXyz mRoomPos;
    u32  mRoomParam;
    f32  mLastSpeedF;
    u32  mLastMode;
    s16  mLastAngleY;
    u8   field_0x22[2];
};

TPHD_ASSERT_OFFSET(dSv_restart_c, mRoomPos, 0x08);
TPHD_ASSERT_OFFSET(dSv_restart_c, mLastMode, 0x1C);
TPHD_ASSERT_SIZE(dSv_restart_c, 0x24);

#define dSv_SAVE_COUNT 32
#define dSv_ZONE_COUNT 32

#define dSv_SERIALIZED_SIZE 0x0DF8u

struct dSv_info_c {
    dSv_player_status_a_c mStatusA;
    u8 field_0x0022[0x0028 - 0x0022];
    u32 mSaveTimestamp[2];
    u8 field_0x0030[0x0058 - 0x0030];
    dSv_player_return_place_c mReturnPlace;
    dSv_player_field_last_stay_info_c mLastStay;
    u8 field_0x0080[0x009C - 0x0080];
    dSv_player_item_c mItem;
    dSv_player_get_item_c mGetItem;
    dSv_player_item_record_c mItemRecord;
    u8 field_0x00f5[0x00F8 - 0x00F5];
    dSv_player_item_max_c mItemMax;
    u8 field_0x0100[0x02F0 - 0x0100];
    dSv_memBit_c mSave[dSv_SAVE_COUNT];
    u8 field_0x06f0[0x07F0 - 0x06F0];
    dSv_event_c mEvent;
    u8 field_0x08f0[0x0DF8 - 0x08F0];
    dSv_memBit_c mMemory;
    u8 mDan[0x0E54 - 0x0E18];
    dSv_zone_c mZone[dSv_ZONE_COUNT];
    dSv_restart_c mRestart;
    dSv_event_c mTmp;
    u8 field_0x1378[0x13D8 - 0x1378];
};

TPHD_ASSERT_OFFSET(dSv_info_c, mSaveTimestamp, 0x0028);
TPHD_ASSERT_OFFSET(dSv_info_c, mReturnPlace, 0x0058);
TPHD_ASSERT_OFFSET(dSv_info_c, mLastStay, 0x0064);
TPHD_ASSERT_OFFSET(dSv_info_c, mItem, 0x009C);
TPHD_ASSERT_OFFSET(dSv_info_c, mGetItem, 0x00CC);
TPHD_ASSERT_OFFSET(dSv_info_c, mItemRecord, 0x00EC);
TPHD_ASSERT_OFFSET(dSv_info_c, mItemMax, 0x00F8);
TPHD_ASSERT_OFFSET(dSv_info_c, mSave, 0x02F0);
TPHD_ASSERT_OFFSET(dSv_info_c, mEvent, 0x07F0);
TPHD_ASSERT_OFFSET(dSv_info_c, mMemory, 0x0DF8);
TPHD_ASSERT_OFFSET(dSv_info_c, mDan, 0x0E18);
TPHD_ASSERT_OFFSET(dSv_info_c, mZone, 0x0E54);
TPHD_ASSERT_OFFSET(dSv_info_c, mRestart, 0x1254);
TPHD_ASSERT_OFFSET(dSv_info_c, mTmp, 0x1278);
TPHD_ASSERT_SIZE(dSv_info_c, 0x13D8);

#define dSv_OFF_HAVE_ARMOR    0x0D1u
#define dSv_OFF_HAVE_SWDSHLD  0x0D2u
#define dSv_OFF_HAVE_MSLIGHT  0x0D6u
#define dSv_OFF_BUGS          0x0E5u
#define dSv_OFF_FUSED_SHADOW  0x109u
#define dSv_OFF_MIRROR        0x10Au
#define dSv_OFF_POE_SOULS     0x10Cu
#define dSv_OFF_SKILL_LO      0x219u
#define dSv_OFF_SKILL_HI      0x21Au
#define dSv_OFF_PLAYER_NAME   0x1B4u
#define dSv_OFF_HORSE_NAME    0x1C5u
#define dSv_NAME_SIZE         0x11u
#define dSv_OFF_LETTER_GET    0x11Cu
#define dSv_OFF_LETTER_READ   0x124u
#define dSv_OFF_LETTER_NUM    0x12Cu
#define dSv_LETTER_COUNT      16
#define dSv_OFF_STAMP         0x938u
#define dSv_STAMP_COUNT       50

#define dSv_IMAGE_CAMERA_CONFIG_OFF 0x1E5u
#define dSv_IMAGE_CAMERA_CONFIG_MAX 3u
