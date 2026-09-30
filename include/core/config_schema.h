#pragma once

#include "core/settings.h"

#include <stddef.h>

namespace Config {
enum FieldType { FIELD_BOOL, FIELD_FLOAT, FIELD_U32 };

struct Field {
    const char* key;
    FieldType   type;
    size_t      offset;
    float       lo, hi;
};

#define TPHD_SETTING(name, type, lo, hi) \
    { #name, type, offsetof(Settings, name), lo, hi }

static const Field kSettingsSchema[] = {
    TPHD_SETTING(drawnScreen,      FIELD_U32,   0.0f,  2.0f),
    TPHD_SETTING(uiScale,          FIELD_FLOAT, 1.0f,  2.0f),
    TPHD_SETTING(hudOnGameScreen,  FIELD_BOOL,  0.0f,  0.0f),
    TPHD_SETTING(hudToTv,          FIELD_BOOL,  0.0f,  0.0f),
    TPHD_SETTING(overlayOpacity,   FIELD_FLOAT, 0.0f,  1.0f),
    TPHD_SETTING(boldLetters,      FIELD_BOOL,  0.0f,  0.0f),
    TPHD_SETTING(freezeOnMenu,     FIELD_BOOL,  0.0f,  0.0f),

    TPHD_SETTING(toastsEnabled,    FIELD_BOOL,  0.0f,  0.0f),
    TPHD_SETTING(toastSeconds,     FIELD_FLOAT, 0.5f, 15.0f),

    TPHD_SETTING(showInitToast,    FIELD_BOOL,  0.0f,  0.0f),

    TPHD_SETTING(watermarkEnabled, FIELD_BOOL,  0.0f,  0.0f),
    TPHD_SETTING(watermarkX,       FIELD_FLOAT, 0.0f,  1.0f),
    TPHD_SETTING(watermarkY,       FIELD_FLOAT, 0.0f,  1.0f),
    TPHD_SETTING(watermarkOpacity, FIELD_FLOAT, 0.05f, 1.0f),
    TPHD_SETTING(watermarkSize,    FIELD_FLOAT, 32.0f, 320.0f),
};

#undef TPHD_SETTING

static const unsigned kSettingsSchemaCount =
    sizeof(kSettingsSchema) / sizeof(kSettingsSchema[0]);
}
