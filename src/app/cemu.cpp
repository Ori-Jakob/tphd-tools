#include "app/cemu.h"
#include "app/code_hooks.h"
#include "app/present.h"

#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <malloc.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <vpad/input.h>

RPL_EXPORT const RplManifest* rpl_manifest();

namespace App {
bool g_underCemu = false;
static uint32_t s_packVersion = 0;

uint32_t PackVersion() { return s_packVersion; }

namespace Cemu {
namespace {
RplHost s_host;
const RplManifest* s_manifest = nullptr;
bool s_ready = false;
bool s_failed = false;
bool s_initialising = false;

RplPad  s_pad;
bool    s_havePad = false;
RplKpad s_kpad[RPL_KPAD_CHANNELS];
bool    s_haveKpad[RPL_KPAD_CHANNELS];
int     s_inputMode = RPL_INPUT_PASS;
bool    s_stickHeld = false;
float   s_stickX = 0.0f, s_stickY = 0.0f;

uint32_t s_buttonsRequested = 0;
uint32_t s_vpadButtonsApplied = 0;
uint32_t s_kpadButtonsApplied[RPL_KPAD_CHANNELS];

const char kToolDir[] = "fs:/vol/external01/tphd_tools";
const char kLogDir[] = "fs:/vol/external01/tphd_tools/logs";

FILE* s_logFile = nullptr;
bool  s_logTried = false;

void openLogFile()
{
    if (s_logTried)
        return;
    s_logTried = true;

    mkdir(kToolDir, 0777);
    mkdir(kLogDir, 0777);

    char path[160];
    snprintf(path, sizeof(path), "%s/%016llX.log", kLogDir,
             (unsigned long long)OSGetTitleID());
    s_logFile = fopen(path, "w");
    if (!s_logFile)
        return;

    OSCalendarTime ct;
    OSTicksToCalendarTime(OSGetTime(), &ct);
    fprintf(s_logFile, "---- session %04d-%02d-%02d %02d:%02d:%02d ----\n",
            ct.tm_year, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min, ct.tm_sec);
    fflush(s_logFile);
    OSReport("[tphd_tools] logging to %s\n", path);
}

void writeLine(const char* line)
{
    if (!s_logFile)
        openLogFile();
    if (!s_logFile)
        return;
    fputs(line, s_logFile);
    fputc('\n', s_logFile);
    fflush(s_logFile);
}

void report(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void report(const char* fmt, ...)
{
    char line[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    OSReport("%s\n", line);
    writeLine(line);
}

void hostLog(const RplHost*, int level, const char* fmt, ...)
{
    char body[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    char line[560];
    snprintf(line, sizeof(line), "[tphd_tools][%s] %s",
             level == RPL_LOG_ERROR ? "error" : level == RPL_LOG_WARN ? "warn" : "info",
             body);
    OSReport("%s\n", line);
    writeLine(line);
}

void hostNotify(const RplHost*, int, const char* text)
{
    char line[256];
    snprintf(line, sizeof(line), "[tphd_tools] %s", text ? text : "");
    OSReport("%s\n", line);
    writeLine(line);
}

uint32_t hostOriginal(const RplHost*, const RplHook*) { return 0; }
int hostState(const RplHost*, const RplHook*) { return RPL_STATE_DECLARED; }

int hostAddHook(const RplHost*, const RplHook* h)
{
    report("[tphd_tools] %s: hooks come from the graphics pack under Cemu",
           h && h->name ? h->name : "?");
    return -5;
}

int hostRemoveHook(const RplHost*, const RplHook*) { return -1; }

void* hostAlloc(const RplHost*, uint32_t size, uint32_t align)
{
    return memalign(align < 4 ? 4 : align, size);
}

void hostFree(const RplHost*, void* p) { free(p); }

uint64_t hostTitleId(const RplHost*)   { return OSGetTitleID(); }
uint32_t hostTextDelta(const RplHost*) { return 0; }
uint32_t hostDataDelta(const RplHost*) { return 0; }

int hostGetBool(const RplHost*, const char*, int def) { return def; }
int hostSetBool(const RplHost*, const char*, int)     { return -1; }
int32_t hostGetInt(const RplHost*, const char*, int32_t def) { return def; }
int hostSetInt(const RplHost*, const char*, int32_t)  { return -1; }

int hostPad(const RplHost*, RplPad* out)
{
    if (!out || !s_havePad)
        return 0;
    *out = s_pad;
    return 1;
}

int hostKpad(const RplHost*, uint32_t chan, RplKpad* out)
{
    if (!out || chan >= RPL_KPAD_CHANNELS || !s_haveKpad[chan])
        return 0;
    *out = s_kpad[chan];
    return 1;
}

void hostSetInputMode(const RplHost*, int mode)
{
    s_inputMode = mode == RPL_INPUT_BLOCK ? RPL_INPUT_BLOCK : RPL_INPUT_PASS;
}

void hostSetStick(const RplHost*, const float* leftXY)
{
    if (!leftXY) {
        s_stickHeld = false;
        return;
    }
    s_stickX = leftXY[0];
    s_stickY = leftXY[1];
    s_stickHeld = true;
}

void hostSetButtons(const RplHost*, uint32_t vpadButtonMask)
{
    s_buttonsRequested = vpadButtonMask;
}

void editButtons(uint32_t& hold, uint32_t& trigger, uint32_t& release,
                 uint32_t current, uint32_t previous)
{
    const uint32_t physicalHold = hold;
    const uint32_t physicalRelease = release;
    trigger &= ~previous;
    release &= ~current;
    trigger |= (current & ~previous) & ~(physicalHold | physicalRelease);
    release |= (previous & ~current) & ~physicalHold;
    hold = physicalHold | current;
}

struct ButtonMap { uint32_t vpad; uint32_t pro; uint32_t classic; };
const ButtonMap kButtonMap[] = {
    { VPAD_BUTTON_A,       WPAD_PRO_BUTTON_A,       WPAD_CLASSIC_BUTTON_A },
    { VPAD_BUTTON_B,       WPAD_PRO_BUTTON_B,       WPAD_CLASSIC_BUTTON_B },
    { VPAD_BUTTON_X,       WPAD_PRO_BUTTON_X,       WPAD_CLASSIC_BUTTON_X },
    { VPAD_BUTTON_Y,       WPAD_PRO_BUTTON_Y,       WPAD_CLASSIC_BUTTON_Y },
    { VPAD_BUTTON_LEFT,    WPAD_PRO_BUTTON_LEFT,    WPAD_CLASSIC_BUTTON_LEFT },
    { VPAD_BUTTON_RIGHT,   WPAD_PRO_BUTTON_RIGHT,   WPAD_CLASSIC_BUTTON_RIGHT },
    { VPAD_BUTTON_UP,      WPAD_PRO_BUTTON_UP,      WPAD_CLASSIC_BUTTON_UP },
    { VPAD_BUTTON_DOWN,    WPAD_PRO_BUTTON_DOWN,    WPAD_CLASSIC_BUTTON_DOWN },
    { VPAD_BUTTON_ZL,      WPAD_PRO_BUTTON_ZL,      WPAD_CLASSIC_BUTTON_ZL },
    { VPAD_BUTTON_ZR,      WPAD_PRO_BUTTON_ZR,      WPAD_CLASSIC_BUTTON_ZR },
    { VPAD_BUTTON_L,       WPAD_PRO_BUTTON_L,       WPAD_CLASSIC_BUTTON_L },
    { VPAD_BUTTON_R,       WPAD_PRO_BUTTON_R,       WPAD_CLASSIC_BUTTON_R },
    { VPAD_BUTTON_PLUS,    WPAD_PRO_BUTTON_PLUS,    WPAD_CLASSIC_BUTTON_PLUS },
    { VPAD_BUTTON_MINUS,   WPAD_PRO_BUTTON_MINUS,   WPAD_CLASSIC_BUTTON_MINUS },
    { VPAD_BUTTON_HOME,    WPAD_PRO_BUTTON_HOME,    WPAD_CLASSIC_BUTTON_HOME },
    { VPAD_BUTTON_STICK_L, WPAD_PRO_BUTTON_STICK_L, 0 },
    { VPAD_BUTTON_STICK_R, WPAD_PRO_BUTTON_STICK_R, 0 },
};

uint32_t mapProButtons(uint32_t vpad)
{
    uint32_t out = 0;
    for (const ButtonMap& m : kButtonMap)
        if (vpad & m.vpad) out |= m.pro;
    return out;
}

uint32_t mapClassicButtons(uint32_t vpad)
{
    uint32_t out = 0;
    for (const ButtonMap& m : kButtonMap)
        if (vpad & m.vpad) out |= m.classic;
    return out;
}

void buildHost()
{
    memset(&s_host, 0, sizeof(s_host));
    s_host.version      = RPL_ABI_VERSION;
    s_host.name         = "tphd_tools";
    s_host.dir          = "fs:/vol/external01/tphd_tools/";
    s_host.log          = hostLog;
    s_host.notify       = hostNotify;
    s_host.original     = hostOriginal;
    s_host.state        = hostState;
    s_host.addHook      = hostAddHook;
    s_host.removeHook   = hostRemoveHook;
    s_host.alloc        = hostAlloc;
    s_host.free         = hostFree;
    s_host.titleId      = hostTitleId;
    s_host.textDelta    = hostTextDelta;
    s_host.dataDelta    = hostDataDelta;
    s_host.getBool      = hostGetBool;
    s_host.setBool      = hostSetBool;
    s_host.pad          = hostPad;
    s_host.kpad         = hostKpad;
    s_host.setInputMode = hostSetInputMode;
    s_host.setStick     = hostSetStick;
    s_host.setButtons   = hostSetButtons;
    s_host.getInt       = hostGetInt;
    s_host.setInt       = hostSetInt;
}

struct PackBlock {
    uint32_t version;
    uint32_t hookCount;
    uint32_t hooks[1];
};

bool s_hooksPublished = false;

void notePackBlock(void* blockPtr)
{
    PackBlock* block = (PackBlock*)blockPtr;
    if (!block)
        return;
    s_packVersion = block->version;
    if (!s_hooksPublished && block->version >= 2) {
        s_hooksPublished = true;
        CodeHooks::Publish(block->hooks, block->hookCount);
    }
}

bool ensureReady()
{
    if (s_ready)
        return true;
    if (s_failed || s_initialising)
        return false;
    s_initialising = true;
    g_underCemu = true;

    s_manifest = rpl_manifest();
    if (!s_manifest || !s_manifest->onInit) {
        s_failed = true;
        return false;
    }

    buildHost();
    const int rc = s_manifest->onInit(&s_host);
    if (rc != 0) {
        report("[tphd_tools] onInit returned %d under Cemu", rc);
        s_failed = true;
        return false;
    }

    s_ready = true;
    return true;
}

void copyToScanBuffer(const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    GX2CopyColorBufferToScanBuffer(buffer, target);
}

void padSampled()
{
    if (s_manifest && s_manifest->onPadSampled)
        s_manifest->onPadSampled();
}

void noteVpad(VPADStatus* buffers, uint32_t count)
{
    if (!buffers || count == 0)
        return;

    const VPADStatus& st = buffers[0];
    s_pad.hold    = st.hold;
    s_pad.trigger = st.trigger;
    s_pad.release = st.release;
    s_pad.leftX   = st.leftStick.x;
    s_pad.leftY   = st.leftStick.y;
    s_pad.rightX  = st.rightStick.x;
    s_pad.rightY  = st.rightStick.y;
    s_pad.touch.x        = st.tpNormal.x;
    s_pad.touch.y        = st.tpNormal.y;
    s_pad.touch.touched  = st.tpNormal.touched;
    s_pad.touch.validity = st.tpNormal.validity;
    s_pad.sample += 1;
    s_havePad = true;

    padSampled();

    const bool block = s_inputMode == RPL_INPUT_BLOCK;
    const uint32_t buttons = s_buttonsRequested;
    const uint32_t previous = s_vpadButtonsApplied;
    s_vpadButtonsApplied = buttons;
    if (!block && !s_stickHeld && buttons == 0 && previous == 0)
        return;

    for (uint32_t i = 0; i < count; ++i) {
        VPADStatus& s = buffers[i];
        if (block) {
            s.hold = s.trigger = s.release = 0;
            s.rightStick.x = s.rightStick.y = 0.0f;
            const uint16_t noPoint = VPAD_INVALID_X | VPAD_INVALID_Y;
            s.tpNormal.touched = 0;
            s.tpNormal.validity = noPoint;
            s.tpFiltered1.touched = 0;
            s.tpFiltered1.validity = noPoint;
            s.tpFiltered2.touched = 0;
            s.tpFiltered2.validity = noPoint;
        }
        if (s_stickHeld) {
            s.leftStick.x = s_stickX;
            s.leftStick.y = s_stickY;
        } else if (block) {
            s.leftStick.x = s.leftStick.y = 0.0f;
        }
        editButtons(s.hold, s.trigger, s.release, buttons, i == 0 ? previous : buttons);
    }
}

void noteKpad(KPADStatus* buffers, uint32_t count, uint32_t chan)
{
    if (!buffers || count == 0 || chan >= RPL_KPAD_CHANNELS)
        return;

    const KPADStatus& st = buffers[0];
    RplKpad k;
    memset(&k, 0, sizeof(k));
    k.extension = (uint32_t)st.extensionType;

    if (st.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        k.hold    = st.pro.hold;
        k.trigger = st.pro.trigger;
        k.release = st.pro.release;
        k.leftX   = st.pro.leftStick.x;
        k.leftY   = st.pro.leftStick.y;
        k.rightX  = st.pro.rightStick.x;
        k.rightY  = st.pro.rightStick.y;
    } else if (st.extensionType == WPAD_EXT_CLASSIC ||
               st.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
        k.hold    = st.classic.hold;
        k.trigger = st.classic.trigger;
        k.release = st.classic.release;
        k.leftX   = st.classic.leftStick.x;
        k.leftY   = st.classic.leftStick.y;
        k.rightX  = st.classic.rightStick.x;
        k.rightY  = st.classic.rightStick.y;
    }

    k.sample = s_kpad[chan].sample + 1;
    s_kpad[chan] = k;
    // Cemu keeps every configured controller live; report an idle one as absent like hardware.
    const bool idle = k.hold == 0 && k.leftX > -0.1f && k.leftX < 0.1f && k.leftY > -0.1f &&
                      k.leftY < 0.1f && k.rightX > -0.1f && k.rightX < 0.1f &&
                      k.rightY > -0.1f && k.rightY < 0.1f;
    s_haveKpad[chan] = !idle;

    padSampled();

    const bool block = s_inputMode == RPL_INPUT_BLOCK;
    const uint32_t buttons = s_buttonsRequested;
    const uint32_t previous = s_kpadButtonsApplied[chan];
    s_kpadButtonsApplied[chan] = buttons;
    if (!block && !s_stickHeld && buttons == 0 && previous == 0)
        return;

    for (uint32_t i = 0; i < count; ++i) {
        KPADStatus& s = buffers[i];
        if (block) {
            s.hold = s.trigger = s.release = 0;
            s.pro.hold = s.pro.trigger = s.pro.release = 0;
            s.pro.rightStick.x = s.pro.rightStick.y = 0.0f;
            s.classic.hold = s.classic.trigger = s.classic.release = 0;
            s.classic.rightStick.x = s.classic.rightStick.y = 0.0f;
            s.nunchuk.hold = s.nunchuk.trigger = s.nunchuk.release = 0;
        }
        if (s_stickHeld) {
            s.pro.leftStick.x = s_stickX;
            s.pro.leftStick.y = s_stickY;
            s.classic.leftStick.x = s_stickX;
            s.classic.leftStick.y = s_stickY;
        } else if (block) {
            s.pro.leftStick.x = s.pro.leftStick.y = 0.0f;
            s.classic.leftStick.x = s.classic.leftStick.y = 0.0f;
            s.nunchuk.stick.x = s.nunchuk.stick.y = 0.0f;
        }
        const uint32_t prev = i == 0 ? previous : buttons;
        if (s.extensionType == WPAD_EXT_PRO_CONTROLLER) {
            editButtons(s.pro.hold, s.pro.trigger, s.pro.release,
                        mapProButtons(buttons), mapProButtons(prev));
        } else if (s.extensionType == WPAD_EXT_CLASSIC ||
                   s.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
            editButtons(s.classic.hold, s.classic.trigger, s.classic.release,
                        mapClassicButtons(buttons), mapClassicButtons(prev));
        }
    }
}
}
}
}

RPL_EXPORT uint32_t rpl_cemu_entry(uint32_t reason, void* a, void* b, void* c)
{
    if (!App::Cemu::ensureReady())
        return 0;

    switch (reason) {
    case RPL_CEMU_FRAME:
        App::Cemu::notePackBlock(a);
        App::Present::OnGameFrame();
        return 1;

    case RPL_CEMU_PRESENT:
        App::Present::OnCopyToScanBuffer(App::Cemu::copyToScanBuffer,
                                         (const GX2ColorBuffer*)a,
                                         (GX2ScanTarget)(uintptr_t)b);
        return 1;

    case RPL_CEMU_VPAD:
        App::Cemu::noteVpad((VPADStatus*)a, (uint32_t)(uintptr_t)b);
        return 1;

    case RPL_CEMU_CONTEXT:
        App::Present::NoteContext((GX2ContextState*)a);
        return 1;

    case RPL_CEMU_KPAD:
        App::Cemu::noteKpad((KPADStatus*)a, (uint32_t)(uintptr_t)b,
                            (uint32_t)(uintptr_t)c);
        return 1;
    }
    return 0;
}
