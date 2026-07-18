// tools/save_load_coords.h -- one-slot Link/camera coordinate save and load.
#pragma once

#include "game/types.h"

namespace Tools {
namespace SaveLoadCoords {

struct SavedCoordinates {
    char stage[9];
    s8 room;
    s8 layer;
    s16 spawn;
    cXyz linkPos;
    s16 linkAngle;
    cXyz camAt;
    cXyz camEye;
};

void DrawMenuItem();
void Tick();
void OnApplicationStart();

bool IsEnabled();
void SetEnabled(bool enabled);

bool GetSavedCoordinates(SavedCoordinates* out);
void SetSavedCoordinates(const SavedCoordinates* saved);

} // namespace SaveLoadCoords
} // namespace Tools
