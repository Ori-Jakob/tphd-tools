#include "core/version.h"

#include <coreinit/debug.h>
#include <coreinit/dynload.h>

#include <rplloader/rplloader.h>

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

static int OnInit(const RplHost* host)
{
    gHost = host;
    host->log(host, RPL_LOG_INFO, "tphd-tools %s by %s, built %s %s",
              TPHD_TOOLS_VERSION, TPHD_TOOLS_AUTHOR, __DATE__, __TIME__);
    return 0;
}

static void OnDeinit()
{
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
    .maxHooks     = 0,
};
}

RPL_EXPORT const RplManifest* rpl_manifest()
{
    return &App::gManifest;
}

RPL_EXPORT int rpl_entry(OSDynLoad_Module module, OSDynLoad_EntryReason reason)
{
    (void)module;
    OSReport("[tphd_tools] rpl_entry(reason %d)\n", (int)reason);
    return 0;
}
