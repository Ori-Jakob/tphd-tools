#include "tools/stage_control.h"

#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/settings.h"
#include "ui/notifications.h"

#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "m_Do/m_Do_Reset.h"

namespace Tools {
namespace StageControl {
void Tick(bool acceptInput)
{
    if (!acceptInput)
        return;

    if (Config::g_settings.reloadHotkey && Hotkeys::Pressed(Hotkeys::HOTKEY_STAGE_RELOAD) &&
        dComIfGp_getPlayer()) {
        Input::DrainHeld();
        dStage_requestReload();
        Notifications::ShowKeyed("stage", Notifications::Info, "Reloading stage");
    }

    if (Config::g_settings.resetHotkey && Hotkeys::Pressed(Hotkeys::HOTKEY_GAME_RESET)) {
        Input::DrainHeld();
        if (!mDoRst_requestReset()) {
            Notifications::ShowKeyed("stage", Notifications::Error, "Game reset unavailable");
            Logger::LogError("[stage] reset requested before the reset data exists");
        }
    }
}
}
}
