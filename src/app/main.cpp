#include "app/cemu.h"
#include "app/hooks.h"
#include "app/present.h"

#include "core/input.h"
#include "core/logger.h"
#include "core/version.h"
#include "ui/notifications.h"
#include "ui/overlay.h"

#include "libtphd/reloc.h"

#include <coreinit/debug.h>
#include <coreinit/dynload.h>

namespace App {
static const RplHost* gHost = nullptr;

struct RplTitleBlob {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
    uint64_t ids[3];
};

static const RplTitleBlob kTitleBlob
    __attribute__((section(".rpltitles"), used, aligned(8))) = {
    RPL_TITLES_MAGIC, 1u, 3u, 0u,
    {
        0x000500001019E500ull,
        0x000500001019E600ull,
        0x000500001019C800ull,
    },
};

#define kTitles      (kTitleBlob.ids)
#define kTitleCount  (sizeof(kTitleBlob.ids) / sizeof(kTitleBlob.ids[0]))

static const char* regionName(uint64_t titleId)
{
    switch (titleId) {
    case 0x000500001019E500ull: return "USA";
    case 0x000500001019E600ull: return "EUR";
    case 0x000500001019C800ull: return "JPN";
    default:                    return "unknown";
    }
}

static int OnInit(const RplHost* host)
{
    gHost = host;

    const uint64_t titleId = host->titleId(host);
    const uint32_t dataDelta = host->dataDelta(host);
    TphdSetGameDataDelta(dataDelta);

    Logger::SetHost(host);
    Logger::Log("tphd-tools %s by %s, built %s %s, %s title, data delta %08X",
                TPHD_TOOLS_VERSION, TPHD_TOOLS_AUTHOR, __DATE__, __TIME__,
                regionName(titleId), (unsigned)dataDelta);

    const bool dynHooked = RegisterDynamicHooks(host);
    Input::SetHost(host);
    Present::Init();
    Ui::Overlay::OnApplicationStart();

    host->log(host, RPL_LOG_INFO, "%u static hook(s), dynamic hooks %s",
              (unsigned)gHookCount, dynHooked ? "ALL APPLIED" : "SOME FAILED");
    return 0;
}

static void OnDeinit()
{
    Present::Shutdown();
    Ui::Overlay::OnApplicationEnd();
    Input::OnApplicationEnd();
    Notifications::Clear();
    gHost = nullptr;
}

static RplManifest gManifest = {
    .magic        = RPL_MAGIC,
    .abiVersion   = RPL_ABI_VERSION,
    .name         = "tphd_tools",
    .version      = TPHD_TOOLS_VERSION,
    .author       = TPHD_TOOLS_AUTHOR,
    .titleIds     = kTitles,
    .titleIdCount = kTitleCount,
    .hooks        = nullptr,
    .hookCount    = 0,
    .priority     = 0,
    .flags        = RPL_FLAG_ALLOW_RELEASE,
    .onInit       = OnInit,
    .onDeinit     = OnDeinit,
    .maxHooks     = 128,
    .onReleaseForeground  = Present::OnReleaseForeground,
    .onAcquiredForeground = Present::OnAcquiredForeground,
    .onPadSampled         = Ui::Overlay::OnPadSampled,
};
}

RPL_EXPORT const RplManifest* rpl_manifest()
{
    App::gManifest.hooks     = App::gHooks;
    App::gManifest.hookCount = App::gHookCount;
    return &App::gManifest;
}

RPL_EXPORT int rpl_entry(OSDynLoad_Module module, OSDynLoad_EntryReason reason)
{
    (void)module;
    OSReport("[tphd_tools] rpl_entry(reason %d)\n", (int)reason);
    return 0;
}
