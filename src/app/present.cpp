#include "app/present.h"

#include "core/logger.h"

#include "m_Do/m_Do_graphic.h"

#include <coreinit/cache.h>

namespace App {
namespace Present {
static volatile bool s_active = false;
static GX2ContextState* s_gameContext = nullptr;
static bool s_contextLogged = false;
static bool s_presentLogged = false;
static uint32_t s_gameFrames = 0;

void Init()
{
    s_gameContext = nullptr;
    s_contextLogged = false;
    s_presentLogged = false;
    s_gameFrames = 0;
    OSMemoryBarrier();
    s_active = true;
}

void Shutdown()
{
    s_active = false;
    OSMemoryBarrier();
}

void OnGameFrame()
{
    if (!s_active)
        return;
    ++s_gameFrames;
}

void OnReleaseForeground()
{
    s_gameContext = nullptr;
}

void OnAcquiredForeground()
{
    s_gameContext = nullptr;
}

void NoteContext(GX2ContextState* state)
{
    if (!s_active || !state)
        return;
    if (!s_contextLogged) {
        s_contextLogged = true;
        Logger::Log("game context %p captured", state);
    }
    s_gameContext = state;
}

void OnCopyToScanBuffer(CopyFn copy, const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    if (!copy)
        return;
    if (s_active && buffer && !s_presentLogged) {
        s_presentLogged = true;
        Logger::Log("first present, %ux%u on %s after %u frames, drc only %d",
                    (unsigned)buffer->surface.width, (unsigned)buffer->surface.height,
                    target == GX2_SCAN_TARGET_TV ? "tv" : "drc", (unsigned)s_gameFrames,
                    (int)mDoGph_isDrcOnly());
    }
    copy(buffer, target);
}
}
}
