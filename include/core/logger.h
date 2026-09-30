#pragma once

struct RplHost;

namespace Logger {
void SetHost(const RplHost* host);

enum Level {
    LL_INFO  = 0,
    LL_WARN  = 1,
    LL_ERROR = 2,
};

void Init();

void OnApplicationStart();

void StartNewLog();

void LogAt(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

void Log(const char* fmt, ...)      __attribute__((format(printf, 1, 2)));
void LogWarn(const char* fmt, ...)  __attribute__((format(printf, 1, 2)));
void LogError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef TPHD_TOOLS_DEBUG
void Breadcrumb(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
}

#ifdef TPHD_TOOLS_DEBUG

#define TPHD_BREADCRUMB(...) Logger::Breadcrumb(__VA_ARGS__)

#define TPHD_BREADCRUMB_EVERY(n, ...)                                       \
    do {                                                                    \
        static unsigned tphdBreadcrumbTick_ = 0;                            \
        const unsigned tphdBreadcrumbEvery_ = (unsigned)(n);                \
        if (tphdBreadcrumbEvery_ == 0 ||                                    \
            (tphdBreadcrumbTick_++ % tphdBreadcrumbEvery_) == 0)            \
            Logger::Log(__VA_ARGS__);                                       \
    } while (0)

#define TPHD_BREADCRUMB_ONCE(...)                                           \
    do {                                                                    \
        static bool tphdBreadcrumbSeen_ = false;                            \
        if (!tphdBreadcrumbSeen_) {                                         \
            tphdBreadcrumbSeen_ = true;                                     \
            Logger::Log(__VA_ARGS__);                                       \
        }                                                                   \
    } while (0)

#else

#define TPHD_BREADCRUMB(...)             ((void)0)
#define TPHD_BREADCRUMB_EVERY(n, ...)    ((void)0)
#define TPHD_BREADCRUMB_ONCE(...)        ((void)0)

#endif
