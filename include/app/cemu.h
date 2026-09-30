#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

enum {
    RPL_CEMU_FRAME   = 0,
    RPL_CEMU_PRESENT = 1,
    RPL_CEMU_VPAD    = 2,
    RPL_CEMU_CONTEXT = 3,
    RPL_CEMU_KPAD    = 4,
};

namespace App {
extern bool g_underCemu;

uint32_t PackVersion();
}

RPL_EXPORT uint32_t rpl_cemu_entry(uint32_t reason, void* a, void* b, void* c);
