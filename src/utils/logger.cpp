// logger.cpp -- async, levelled TPHD Tools file logger.
#include "logger.h"

#include "storage.h"
#include "version.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <coreinit/debug.h>
#include <coreinit/messagequeue.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

namespace Logger {

enum { MSG_ROTATE = 0, MSG_WRITE = 1 };

struct LogMsg {
    int  type;
#ifdef TPHD_TOOLS_DEBUG
    volatile int* completion;
#endif
    char text[512];
};

static OSThread       s_thread;
static bool           s_threadStarted = false;
static OSMessageQueue s_queue;
static OSMessage      s_msgBuf[64];
static __attribute__((aligned(16))) uint8_t s_stack[12 * 1024];

static const char* levelTag(Level level)
{
    switch (level) {
    case LL_WARN:  return "WARN ";
    case LL_ERROR: return "ERROR";
    case LL_INFO:
    default:       return "INFO ";
    }
}

static bool enqueue(LogMsg* item, bool blocking = false)
{
    if (!item)
        return false;
    Init();
    if (!s_threadStarted)
        return false;

    OSMessage msg;
    msg.message = item;
    msg.args[0] = msg.args[1] = msg.args[2] = 0;
    return OSSendMessage(&s_queue, &msg,
                         blocking ? OS_MESSAGE_FLAGS_BLOCKING : OS_MESSAGE_FLAGS_NONE) != 0;
}

static int worker(int argc, const char** argv)
{
    (void)argc; (void)argv;

    bool prepared = false;
    bool rotatePending = false;

    for (;;) {
        OSMessage msg;
        OSReceiveMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        LogMsg* item = (LogMsg*)msg.message;
        if (!item)
            continue;

        if (item->type == MSG_ROTATE) {
            prepared = Storage::PrepareLog();
            rotatePending = !prepared;
#ifdef TPHD_TOOLS_DEBUG
            if (item->completion)
                *item->completion = 1;
#endif
            free(item);
            continue;
        }

        if (!prepared || rotatePending) {
            prepared = Storage::PrepareLog();
            rotatePending = !prepared;
        }

        if (prepared)
            Storage::AppendLog(item->text, (uint32_t)strlen(item->text));
#ifdef TPHD_TOOLS_DEBUG
        if (item->completion)
            *item->completion = 1;
#endif
        free(item);
    }

    return 0;
}

void OnApplicationStart()
{
    // The worker thread was created inside the previous game process and died
    // with it; only the plugin's statics survived. Drop the started flag so the
    // next enqueue re-initializes the queue and creates a fresh thread in the
    // NEW process. Any messages still queued at exit are intentionally NOT
    // freed: they were malloc'd in the dead process, so the pointers are no
    // longer ours to free -- re-initializing the queue just forgets them.
    s_threadStarted = false;
}

void Init()
{
    if (s_threadStarted)
        return;

    OSInitMessageQueue(&s_queue, s_msgBuf, (int32_t)(sizeof(s_msgBuf) / sizeof(s_msgBuf[0])));
    void* stackTop = s_stack + sizeof(s_stack);
    if (!OSCreateThread(&s_thread, worker, 0, nullptr, stackTop, sizeof(s_stack), 16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        OSReport("[tphd_tools] logger: worker thread create failed\n");
        return;
    }
    OSSetThreadName(&s_thread, "tphd_tools_log");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
}

void StartNewLog()
{
    LogMsg* rotate = (LogMsg*)malloc(sizeof(LogMsg));
    if (rotate) {
        rotate->type = MSG_ROTATE;
#ifdef TPHD_TOOLS_DEBUG
        rotate->completion = nullptr;
#endif
        rotate->text[0] = '\0';
        if (!enqueue(rotate))
            free(rotate);
    }
    LogAt(LL_INFO, "===== Welcome to TPHD Tools %s by %s =====",
          TPHD_TOOLS_VERSION, TPHD_TOOLS_AUTHOR);
}

// Build "[HH:MM:SS] [LEVEL] <msg>\n", mirror to OSReport, and enqueue for the file.
static void emit(Level level, const char* fmt, va_list args, bool synchronous = false)
{
    if (!fmt)
        return;

    LogMsg* item = (LogMsg*)malloc(sizeof(LogMsg));
    if (!item)
        return;

    item->type = MSG_WRITE;
#ifdef TPHD_TOOLS_DEBUG
    volatile int completion = 0;
    item->completion = synchronous ? &completion : nullptr;
#else
    (void)synchronous;
#endif

    OSCalendarTime ct;
    OSTicksToCalendarTime(OSGetTime(), &ct);
    int off = snprintf(item->text, sizeof(item->text), "[%02d:%02d:%02d] [%s] ",
                       ct.tm_hour, ct.tm_min, ct.tm_sec, levelTag(level));
    if (off < 0 || off >= (int)sizeof(item->text))
        off = 0;

    int n = vsnprintf(item->text + off, sizeof(item->text) - (size_t)off, fmt, args);
    if (n < 0)
        item->text[off] = '\0';

    size_t len = strlen(item->text);
    if (len == 0 || item->text[len - 1] != '\n') {
        if (len + 1 < sizeof(item->text)) {
            item->text[len++] = '\n';
            item->text[len] = '\0';
        } else {
            item->text[sizeof(item->text) - 2] = '\n';
            item->text[sizeof(item->text) - 1] = '\0';
        }
    }

    OSReport("%s", item->text);
    if (!enqueue(item, synchronous)) {
        free(item);
        return;
    }

#ifdef TPHD_TOOLS_DEBUG
    if (synchronous)
        while (!completion)
            OSYieldThread();
#endif
}

void LogAt(Level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(level, fmt, args);
    va_end(args);
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_INFO, fmt, args);
    va_end(args);
}

void LogWarn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_WARN, fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_ERROR, fmt, args);
    va_end(args);
}

#ifdef TPHD_TOOLS_DEBUG
void Breadcrumb(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_INFO, fmt, args, true);
    va_end(args);
}
#endif

} // namespace Logger
