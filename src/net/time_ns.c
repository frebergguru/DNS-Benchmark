#include "time_ns.h"

#ifdef _WIN32
#include <windows.h>
uint64_t dnsb_now_ns(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    /* Split the multiplication to avoid overflowing uint64 after ~15 minutes
       of uptime on a 10 MHz QPC. (now.QuadPart * 1e9) can exceed 2^63 quickly;
       this form keeps both factors well under that. */
    uint64_t qpc = (uint64_t)now.QuadPart;
    uint64_t f   = (uint64_t)freq.QuadPart;
    return (qpc / f) * 1000000000ULL + ((qpc % f) * 1000000000ULL) / f;
}
#else
#include <time.h>
uint64_t dnsb_now_ns(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif
