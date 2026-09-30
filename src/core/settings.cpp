#include "core/settings.h"

#include "cheats/cheats.h"
#include "core/hotkeys.h"
#include "core/rebind.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "tools/coordinates.h"
#include "tools/flycam.h"
#include "tools/modern_camera.h"
#include "tools/practice_timer.h"
#include "tools/roll_checker.h"
#include "tools/save_states.h"
#include "ui/menu_nav.h"
#include "ui/quick_access.h"

namespace Config {
Settings g_settings;

void ResetToDefaults()
{
    Settings& s = g_settings;
    s.drawnScreen     = DRAWN_SCREEN_BOTH;
    s.uiScale         = 1.0f;
    s.hudOnGameScreen = true;
    s.hudToTv         = false;
    s.overlayOpacity  = 1.0f;
    s.boldLetters     = true;
    s.freezeOnMenu    = false;
    s.controller      = 0;
    s.reloadHotkey    = true;
    s.resetHotkey     = true;

    s.toastsEnabled   = true;
    s.toastSeconds    = 3.0f;

    s.showInitToast   = true;

    s.watermarkEnabled = true;
    s.watermarkX       = 0.012f;
    s.watermarkY       = 0.020f;
    s.watermarkOpacity = 0.35f;
    s.watermarkSize    = 128.0f;

    Rebind::Cancel();
    Hotkeys::ResetToDefaults();
    Ui::Nav::ResetBindings();
    Cheats::ResetToDefaults();
    Ui::QuickAccess::ResetToDefaults();
    Hud::GameInfo::ResetToDefaults();
    Hud::InputViewer::ResetToDefaults();
    Tools::RollChecker::ResetToDefaults();
    Tools::PracticeTimer::ResetToDefaults();
    Tools::FlyCam::ResetToDefaults();
    Tools::ModernCamera::ResetToDefaults();
    Tools::Coordinates::ResetToDefaults();
    Tools::SaveStates::ResetToDefaults();
}
}
