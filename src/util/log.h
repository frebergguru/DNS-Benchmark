#ifndef DNSB_UTIL_LOG_H
#define DNSB_UTIL_LOG_H

#include <stdarg.h>

typedef enum { DNSB_LOG_DEBUG = 0, DNSB_LOG_INFO, DNSB_LOG_WARN, DNSB_LOG_ERROR } dnsb_log_level;

void dnsb_log_set_level(dnsb_log_level lvl);
void dnsb_log_msg(dnsb_log_level lvl, const char *file, int line, const char *fmt, ...);

#define DNSB_DEBUG(...) dnsb_log_msg(DNSB_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define DNSB_INFO(...)  dnsb_log_msg(DNSB_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define DNSB_WARN(...)  dnsb_log_msg(DNSB_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define DNSB_ERROR(...) dnsb_log_msg(DNSB_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
