#include "cheats/cheats.h"

#include "cheats/cheat_equipment.h"
#include "cheats/cheat_player.h"

namespace Cheats {
void Tick(bool acceptInput)
{
    Player::Tick(acceptInput);
    Equipment::Tick(acceptInput);
}

bool WantsInputBlock(uint32_t held)
{
    return Player::WantsInputBlock(held);
}

void ResetToDefaults()
{
    Player::ResetToDefaults();
    Equipment::ResetToDefaults();
}
}
