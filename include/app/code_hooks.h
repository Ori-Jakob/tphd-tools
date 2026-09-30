#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

namespace App {
namespace CodeHooks {
uint32_t Count();

bool Install(const RplHost* host);
bool Publish(uint32_t* table, uint32_t count);
}
}
