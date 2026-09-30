#include "cheats/cheat_camera.h"

#include "core/config.h"

#include "d/d_camera.h"

#include <coreinit/cache.h>
#include <stdint.h>

extern "C" {
volatile uint8_t g_tphdFreeFixedCameraEnabled = 0;
volatile uint8_t g_tphdFreeFixedCameraScope = 0;

static int followStyle(const dCamType_c* types, int type, int isWolf)
{
    return types[type].mStyle[isWolf ? 1 : 0][dCam_MODE_FOLLOW];
}

static bool isStatic(int algorithm)
{
    return algorithm == dCam_ALG_FIXED_POS || algorithm == dCam_ALG_FIXED_FRAME ||
           algorithm == dCam_ALG_RAIL;
}

int tphdCameraFilterFixedType(int candidate, dCamera_c* camera)
{
    if (!g_tphdFreeFixedCameraEnabled || !camera || candidate < 0)
        return candidate;

    const dCamType_c* types = camera->mCamTypeData;
    const dCamParam_c* styles = camera->mCamStyleData;
    const int typeCount = camera->mCamTypeCount;
    if (!types || !styles || (typeCount > 0 && candidate >= typeCount))
        return candidate;

    const bool haveTag = camera->mTagCamTool.mCameraIndex != dCam_MAPTOOL_NONE;
    const bool haveRoom = camera->mRoomMapTool.mCameraIndex != dCam_MAPTOOL_NONE;
    const uint8_t scope = g_tphdFreeFixedCameraScope;
    const bool inScope = scope >= Cheats::Camera::SCOPE_EVERYWHERE ||
                         (scope == Cheats::Camera::SCOPE_ROOMS ? haveTag || haveRoom : haveTag);
    if (!inScope)
        return candidate;

    const int isWolf = camera->mIsWolf;
    const int style = followStyle(types, candidate, isWolf);
    if (style < 0 || !isStatic(dCam_getStyleAlgorithm(&styles[style])))
        return candidate;

    const int fallback = camera->mDefaultType;
    if (fallback < 0 || fallback == candidate || (typeCount > 0 && fallback >= typeCount))
        return candidate;
    const int fallbackStyle = followStyle(types, fallback, isWolf);
    if (fallbackStyle < 0 || isStatic(dCam_getStyleAlgorithm(&styles[fallbackStyle])))
        return candidate;
    return fallback;
}
}

namespace Cheats {
namespace Camera {
static bool s_enabled = false;
static int  s_scope = SCOPE_REGIONS;

bool FreeFixedCameraEnabled()               { return s_enabled; }
void SetFreeFixedCameraEnabled(bool enabled) { s_enabled = enabled; }
int  GetScope()                             { return s_scope; }

void SetScope(int scope)
{
    if (scope >= 0 && scope < SCOPE_COUNT)
        s_scope = scope;
}

static void publish(volatile uint8_t* target, uint8_t value)
{
    if (*target == value)
        return;
    *target = value;
    DCFlushRange((void*)target, sizeof(*target));
}

void Tick()
{
    publish(&g_tphdFreeFixedCameraScope, (uint8_t)s_scope);
    publish(&g_tphdFreeFixedCameraEnabled, s_enabled ? 1 : 0);
}

void ResetToDefaults()
{
    s_enabled = false;
    s_scope = SCOPE_REGIONS;
}

void LoadConfig(const cJSON* root)
{
    Config::ReadBool(root, "cameraFreeFixed", &s_enabled);
    Config::ReadInt(root, "cameraFreeFixedScope", &s_scope, 0, SCOPE_COUNT - 1);
}

void SaveConfig(cJSON* root)
{
    Config::AddBool(root, "cameraFreeFixed", s_enabled);
    Config::AddNumber(root, "cameraFreeFixedScope", s_scope);
}
}
}
