#pragma once

namespace Controller {
enum Preference {
    PREFERENCE_AUTO = 0,
    PREFERENCE_GAMEPAD,
    PREFERENCE_PRO,
};

void EnsureSelected();
void Apply();
}
