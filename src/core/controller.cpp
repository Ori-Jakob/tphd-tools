#include "core/controller.h"

#include "core/logger.h"
#include "core/settings.h"

#include "m_Do/m_Do_controller_pad.h"

namespace Controller {
static const char* modeName(u32 mode)
{
    return mode == (u32)mDoCPd_MODE_PRO ? "Pro Controller" : "GamePad";
}

void EnsureSelected()
{
    const u32 mode = mDoCPd_ensureControllerSelected((int)Config::g_settings.controller);
    if (mode)
        Logger::Log("[controller] %s selected for the load", modeName(mode));
}

void Apply()
{
    const u32 mode = mDoCPd_setController((int)Config::g_settings.controller);
    if (mode)
        Logger::Log("[controller] switched to %s", modeName(mode));
}
}
