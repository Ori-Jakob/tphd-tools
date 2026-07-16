// logger.h -- async, levelled file logger for TPHD Tools.
//
// Every line is mirrored to OSReport (visible in the Cemu log / a console syslog)
// AND queued to a worker thread that appends it to tphd_tools/log.txt through the
// build-specific Storage backend. Callers only format + enqueue; no file I/O runs
// on the game thread.
//
// Levels: INFO (normal), WARN (recoverable/unexpected), ERROR (operation failed).
// Use the level helpers; Logger::Log stays as INFO for existing call sites.
#pragma once

namespace Logger {

enum Level {
    LL_INFO  = 0,
    LL_WARN  = 1,
    LL_ERROR = 2,
};

// Start the worker if needed. Safe to call more than once.
void Init();

// Aroma only: the plugin's statics outlive the game process, but the worker
// thread (and any queued messages) die with it. Call at every application
// start so the next Log/Init re-creates the thread and queue in the new
// process instead of enqueueing into a dead one forever.
void OnApplicationStart();

// Begin a fresh boot log: log.txt.old is replaced by the current log.txt, then
// a new log.txt is created. Safe to call once per game/application boot.
void StartNewLog();

// Core entry: format + enqueue one line at `level`. A trailing newline is added
// if the format omits it. NULL fmt is ignored.
void LogAt(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Convenience wrappers.
void Log(const char* fmt, ...)      __attribute__((format(printf, 1, 2)));  // INFO
void LogWarn(const char* fmt, ...)  __attribute__((format(printf, 1, 2)));
void LogError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef TPHD_TOOLS_DEBUG
// Crash-localization checkpoint. Unlike the normal async helpers, this waits
// until the logger worker has attempted the log.txt append before returning.
// The line is always mirrored to OSReport first.
void Breadcrumb(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif

} // namespace Logger

#ifdef TPHD_TOOLS_DEBUG
#define TPHD_BREADCRUMB(...) Logger::Breadcrumb(__VA_ARGS__)
#else
#define TPHD_BREADCRUMB(...) ((void)0)
#endif
