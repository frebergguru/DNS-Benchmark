#ifndef DNSB_ENGINE_STATS_H
#define DNSB_ENGINE_STATS_H

#include <stddef.h>

typedef struct {
    double *samples;
    size_t  n;
    size_t  cap;
    double  min_ms;
    double  max_ms;
    double  sum_ms;
    double  sum_sq_ms;
} dnsb_stats;

void   dnsb_stats_init(dnsb_stats *s);
void   dnsb_stats_free(dnsb_stats *s);
void   dnsb_stats_reset(dnsb_stats *s);
int    dnsb_stats_add(dnsb_stats *s, double sample_ms);

double dnsb_stats_mean(const dnsb_stats *s);
double dnsb_stats_stddev(const dnsb_stats *s);
double dnsb_stats_median(dnsb_stats *s);          /* mutates: keeps samples sorted */
static inline size_t dnsb_stats_count(const dnsb_stats *s) { return s->n; }

#endif
