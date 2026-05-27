#include "stats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void dnsb_stats_init(dnsb_stats *s) {
    memset(s, 0, sizeof(*s));
}

void dnsb_stats_free(dnsb_stats *s) {
    free(s->samples);
    memset(s, 0, sizeof(*s));
}

void dnsb_stats_reset(dnsb_stats *s) {
    s->n = 0;
    s->min_ms = 0;
    s->max_ms = 0;
    s->sum_ms = 0;
    s->sum_sq_ms = 0;
}

int dnsb_stats_add(dnsb_stats *s, double sample_ms) {
    if (s->n == s->cap) {
        size_t newcap = s->cap ? s->cap * 2 : 64;
        double *p = realloc(s->samples, newcap * sizeof(double));
        if (!p) return -1;
        s->samples = p;
        s->cap = newcap;
    }
    s->samples[s->n++] = sample_ms;
    if (s->n == 1 || sample_ms < s->min_ms) s->min_ms = sample_ms;
    if (s->n == 1 || sample_ms > s->max_ms) s->max_ms = sample_ms;
    s->sum_ms    += sample_ms;
    s->sum_sq_ms += sample_ms * sample_ms;
    return 0;
}

double dnsb_stats_mean(const dnsb_stats *s) {
    return s->n ? s->sum_ms / (double)s->n : 0.0;
}

double dnsb_stats_stddev(const dnsb_stats *s) {
    if (s->n < 2) return 0.0;
    double m = dnsb_stats_mean(s);
    double var = (s->sum_sq_ms - (double)s->n * m * m) / (double)(s->n - 1);
    if (var < 0) var = 0;
    return sqrt(var);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

double dnsb_stats_median(dnsb_stats *s) {
    if (s->n == 0) return 0.0;
    qsort(s->samples, s->n, sizeof(double), cmp_double);
    if (s->n % 2) return s->samples[s->n / 2];
    return 0.5 * (s->samples[s->n / 2 - 1] + s->samples[s->n / 2]);
}
