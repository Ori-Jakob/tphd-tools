#pragma once

#include <stdint.h>

namespace Config {
enum DrawnScreen {
    DRAWN_SCREEN_BOTH    = 0,
    DRAWN_SCREEN_GAMEPAD = 1,
    DRAWN_SCREEN_TV      = 2,
};

struct Settings {
    uint32_t drawnScreen;
    float    uiScale;
    bool     hudOnGameScreen;
    bool     hudToTv;
    float    overlayOpacity;
    bool     boldLetters;
    bool     freezeOnMenu;
    uint32_t controller;
    bool     reloadHotkey;
    bool     resetHotkey;

    bool     toastsEnabled;
    float    toastSeconds;

    bool     showInitToast;

    bool     watermarkEnabled;
    float    watermarkX;
    float    watermarkY;
    float    watermarkOpacity;
    float    watermarkSize;
};

extern Settings g_settings;

void ResetToDefaults();
}
