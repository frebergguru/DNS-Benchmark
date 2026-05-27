/* Headless engine smoke check. Runs a tiny multi-resolver benchmark and
   verifies that at least one resolver returns a positive cached timing.
   Skipped via SKIP_NETWORK_TESTS=1. */
#include "engine/engine.h"
#include "net/socket_compat.h"
#include "util/log.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GMainLoop *loop = NULL;

static void on_event(const dnsb_event *evt, void *user) {
    (void)user;
    if (evt->kind == DNSB_EVT_RUN_DONE) g_main_loop_quit(loop);
}

static gboolean quit_loop(gpointer data) {
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

static dnsb_resolver *make_resolver(const char *name, const char *addr,
                                    dnsb_transport_kind t, int port,
                                    const char *hostname) {
    dnsb_resolver *r = calloc(1, sizeof(*r));
    r->name      = g_strdup(name);
    r->owner     = g_strdup("test");
    r->addr      = g_strdup(addr);
    r->hostname  = hostname ? g_strdup(hostname) : NULL;
    r->transport = t;
    r->port      = port;
    return r;
}

int main(void) {
    if (getenv("SKIP_NETWORK_TESTS")) {
        printf("smoke_engine: SKIP (SKIP_NETWORK_TESTS set)\n");
        return 77;
    }
    dnsb_log_set_level(DNSB_LOG_WARN);
    if (dnsb_net_init() != 0) { fprintf(stderr, "net_init failed\n"); return 1; }

    dnsb_engine *e = dnsb_engine_new();
    dnsb_engine_config c = dnsb_engine_default_config();
    c.query_sets = 5;
    c.spacing_ms = 5;
    c.timeout_ms = 1500;
    c.concurrency = 4;
    c.probe_redirection = 0;
    dnsb_engine_set_config(e, &c);

    const char *domains[] = { "wikipedia.org", "github.com" };
    dnsb_engine_set_domains(e, domains, 2);

    dnsb_engine_add_resolver(e, make_resolver("CF UDP", "1.1.1.1", DNSB_TRANSPORT_UDP, 53,  NULL));
    dnsb_engine_add_resolver(e, make_resolver("G  UDP", "8.8.8.8", DNSB_TRANSPORT_UDP, 53,  NULL));
    dnsb_engine_add_resolver(e, make_resolver("Q9 UDP", "9.9.9.9", DNSB_TRANSPORT_UDP, 53,  NULL));
    dnsb_engine_add_resolver(e, make_resolver("CF TCP", "1.1.1.1", DNSB_TRANSPORT_TCP, 53,  NULL));
    dnsb_engine_add_resolver(e, make_resolver("CF DoH", "1.1.1.1", DNSB_TRANSPORT_DOH, 443, "cloudflare-dns.com"));
    dnsb_engine_add_resolver(e, make_resolver("CF DoT", "1.1.1.1", DNSB_TRANSPORT_DOT, 853, "cloudflare-dns.com"));

    dnsb_engine_set_callback(e, on_event, NULL);

    loop = g_main_loop_new(NULL, FALSE);
    if (dnsb_engine_start(e) != 0) { fprintf(stderr, "start failed\n"); return 1; }

    g_timeout_add_seconds(20, quit_loop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    /* If we exited via the 20s watchdog rather than DNSB_EVT_RUN_DONE,
       workers may still be in flight. Stop and join them before reading
       per-resolver stats, otherwise we'd race the same realloc-during-
       qsort the engine's stats_mutex exists to prevent. */
    dnsb_engine_stop(e);

    int any_ok = 0;
    for (size_t i = 0; i < dnsb_engine_resolver_count(e); i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(e, i);
        g_mutex_lock(&r->stats_mutex);
        int qok = r->queries_ok, qsent = r->queries_sent;
        double mc = dnsb_stats_mean(&r->cached);
        double mu = dnsb_stats_mean(&r->uncached);
        g_mutex_unlock(&r->stats_mutex);
        printf("  %-12s ok=%d sent=%d cached=%.2fms uncached=%.2fms\n",
               r->name, qok, qsent, mc, mu);
        if (qok > 0 && (mc > 0 || mu > 0)) any_ok = 1;
    }

    dnsb_engine_free(e);
    dnsb_net_shutdown();

    if (!any_ok) { fprintf(stderr, "smoke_engine: no resolver returned timings\n"); return 1; }
    printf("smoke_engine: OK\n");
    return 0;
}
