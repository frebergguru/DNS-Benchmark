#include "log.h"

#include <stdio.h>
#include <time.h>

static dnsb_log_level g_level = DNSB_LOG_INFO;

void dnsb_log_set_level(dnsb_log_level lvl) { g_level = lvl; }

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
    if (lvl < g_level) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    fprintf(stderr, "%02d:%02d:%02d.%03ld %s %s:%d ",
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
            level_str(lvl), file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
