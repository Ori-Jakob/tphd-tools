#include "cheats/cheats.h"

#include "cheats/cheat_camera.h"
#include "cheats/cheat_difficulty.h"
#include "cheats/cheat_equipment.h"
#include "cheats/cheat_minigames.h"
#include "cheats/cheat_movement.h"
#include "cheats/cheat_player.h"

namespace Cheats {
void Tick(bool acceptInput)
{
    Player::Tick(acceptInput);
    Equipment::Tick(acceptInput);
    Difficulty::Tick();
    Minigames::Tick(!acceptInput);
    Movement::Tick();
    Camera::Tick();
}

void OnApplicationStart()
{
    Movement::OnApplicationStart();
}

bool WantsInputBlock(uint32_t held)
{
    return Player::WantsInputBlock(held);
}

void ResetToDefaults()
{
    Player::ResetToDefaults();
    Equipment::ResetToDefaults();
    Difficulty::ResetToDefaults();
    Minigames::ResetToDefaults();
    Movement::ResetToDefaults();
    Camera::ResetToDefaults();
}
}
