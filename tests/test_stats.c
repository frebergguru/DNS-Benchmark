#include "engine/stats.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_empty(void) {
    dnsb_stats s;
    dnsb_stats_init(&s);
    assert(dnsb_stats_count(&s) == 0);
    assert(dnsb_stats_mean(&s) == 0.0);
    assert(dnsb_stats_stddev(&s) == 0.0);
    dnsb_stats_free(&s);
}

static void test_basic(void) {
    dnsb_stats s;
    dnsb_stats_init(&s);
    double xs[] = { 10, 20, 30, 40, 50 };
    for (int i = 0; i < 5; i++) assert(dnsb_stats_add(&s, xs[i]) == 0);
    assert(dnsb_stats_count(&s) == 5);
    assert(fabs(dnsb_stats_mean(&s) - 30.0) < 1e-9);
    /* sample stddev of {10,20,30,40,50} = sqrt(250) ~= 15.811388 */
    assert(fabs(dnsb_stats_stddev(&s) - 15.811388300841896) < 1e-6);
    assert(s.min_ms == 10.0);
    assert(s.max_ms == 50.0);
    assert(dnsb_stats_median(&s) == 30.0);
    dnsb_stats_free(&s);
}

static void test_median_even(void) {
    dnsb_stats s;
    dnsb_stats_init(&s);
    double xs[] = { 1, 2, 3, 4 };
    for (int i = 0; i < 4; i++) dnsb_stats_add(&s, xs[i]);
    assert(dnsb_stats_median(&s) == 2.5);
    dnsb_stats_free(&s);
}

int main(void) {
    test_empty();
    test_basic();
    test_median_even();
    printf("test_stats: OK\n");
    return 0;
}
