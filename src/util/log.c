#include "log.h"

#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

/* Workers and the main thread both call into the log; the level mutates
   from the UI ("set verbose") and is read on every log call. Atomic with
   relaxed ordering is enough — we only need single-int read/write atomicity,
   not synchronisation with other state. */
static atomic_int g_level = DNSB_LOG_INFO;

void dnsb_log_set_level(dnsb_log_level lvl) {
    atomic_store_explicit(&g_level, (int)lvl, memory_order_relaxed);
}

static const char *level_str(dnsb_log_level lvl) {
    switch (lvl) {
    case DNSB_LOG_DEBUG: return "DEBUG";
    case DNSB_LOG_INFO:  return "INFO ";
    case DNSB_LOG_WARN:  return "WARN ";
    case DNSB_LOG_ERROR: return "ERROR";
    }
    return "?    ";
}

void dnsb_log_msg(dnsb_log_level lvl, const char *file, int line, const char *fmt, ...) {
    int cur = atomic_load_explicit(&g_level, memory_order_relaxed);
    if ((int)lvl < cur) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    /* Hold stderr's lock across the prefix + body + newline so concurrent
       workers don't interleave mid-line. flockfile/funlockfile are POSIX;
       on MSYS2 mingw, _lock_file/_unlock_file would be needed but glib's
       stdio routes through the same lock so this stays in-tree. */
#if !defined(_WIN32)
    flockfile(stderr);
#endif
    fprintf(stderr, "%02d:%02d:%02d.%03ld %s %s:%d ",
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
            level_str(lvl), file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
#if !defined(_WIN32)
    funlockfile(stderr);
#endif
}
