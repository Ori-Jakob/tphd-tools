#include "app/hooks.h"
#include "app/cemu.h"
#include "app/code_hooks.h"
#include "app/present.h"

#include <gx2/context.h>
#include <gx2/surface.h>
#include <gx2/swap.h>

namespace App {
RPL_DECL_REPLACE(void, cCt_Counter, int reset)
{
    real_cCt_Counter(reset);
    Present::OnGameFrame();
}

RPL_DECL_REPLACE(void, GX2CopyColorBufferToScanBuffer,
                 const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    Present::OnCopyToScanBuffer(real_GX2CopyColorBufferToScanBuffer, buffer, target);
}

RPL_DECL_REPLACE(void, GX2SetContextState, GX2ContextState* state)
{
    Present::NoteContext(state);
    if (real_GX2SetContextState)
        real_GX2SetContextState(state);
}

bool RegisterDynamicHooks(const RplHost* host)
{
    if (!host)
        return false;
    if (g_underCemu) {
        host->log(host, RPL_LOG_INFO, "hooks come from the Cemu graphics pack");
        return true;
    }
    return CodeHooks::Install(host);
}

const RplHook gHooks[] = {
    RPL_REPLACE(cCt_Counter, 0x02BEF838u, 0x3D401029u, RPL_HOOK_REQUIRED),
    RPL_REPLACE_LIB(GX2CopyColorBufferToScanBuffer, "gx2", RPL_HOOK_REQUIRED),
    RPL_REPLACE_LIB(GX2SetContextState, "gx2", RPL_HOOK_REQUIRED),
};
const uint32_t gHookCount = sizeof(gHooks) / sizeof(gHooks[0]);
}
