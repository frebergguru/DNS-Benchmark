#ifndef DNSB_TIME_NS_H
#define DNSB_TIME_NS_H

#include <stdint.h>

/* Monotonic, high-resolution clock for benchmarking. */
uint64_t dnsb_now_ns(void);

static inline double dnsb_ns_to_ms(uint64_t ns) { return (double)ns / 1.0e6; }

#endif
