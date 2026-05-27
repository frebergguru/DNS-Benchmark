#include "engine.h"

#include "../dns/packet.h"
#include "../net/socket_compat.h"
#include "../net/time_ns.h"
#include "../util/log.h"
#include "../util/strings.h"

#include <glib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_THROTTLE_NS (100ULL * 1000000ULL)   /* 100 ms */

typedef struct {
    dnsb_resolver **items;
    size_t n, cap;
} resolver_vec;

struct dnsb_engine {
    dnsb_engine_config cfg;
    resolver_vec resolvers;

    /* Test domains for uncached lookups. */
    char **uncached_pool;
    size_t uncached_pool_n;

    dnsb_event_cb cb;
    void *cb_user;

    GThread *workers;            /* fixed-size array */
    size_t   worker_count;
    GThreadPool *pool;           /* if using a pool */

    atomic_int  cancel;
    atomic_int  running;
    atomic_int  outstanding;     /* resolvers still working */

    GMutex      mutex;
    GMutex      callback_mutex;
};

static void resolver_vec_push(resolver_vec *v, dnsb_resolver *r) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->items = realloc(v->items, v->cap * sizeof(*v->items));
    }
    v->items[v->n++] = r;
}

void dnsb_doh_free_state(void *s);
void dnsb_dot_free_state(void *s);

static void resolver_free_contents(dnsb_resolver *r) {
    if (r->transport_state) {
        switch (r->transport) {
            case DNSB_TRANSPORT_DOH: dnsb_doh_free_state(r->transport_state); break;
            case DNSB_TRANSPORT_DOT: dnsb_dot_free_state(r->transport_state); break;
            default: break;
        }
        r->transport_state = NULL;
    }
    free(r->name);
    free(r->owner);
    free(r->addr);
    free(r->hostname);
    dnsb_stats_free(&r->cached);
    dnsb_stats_free(&r->uncached);
    dnsb_stats_free(&r->dotcom);
    g_mutex_clear(&r->stats_mutex);
}

dnsb_engine *dnsb_engine_new(void) {
    dnsb_engine *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->cfg = dnsb_engine_default_config();
    g_mutex_init(&e->mutex);
    g_mutex_init(&e->callback_mutex);
    atomic_init(&e->cancel, 0);
    atomic_init(&e->running, 0);
    atomic_init(&e->outstanding, 0);
    return e;
}

void dnsb_engine_free(dnsb_engine *e) {
    if (!e) return;
    dnsb_engine_stop(e);
    for (size_t i = 0; i < e->resolvers.n; i++) {
        resolver_free_contents(e->resolvers.items[i]);
        free(e->resolvers.items[i]);
    }
    free(e->resolvers.items);
    for (size_t i = 0; i < e->uncached_pool_n; i++) free(e->uncached_pool[i]);
    free(e->uncached_pool);
    g_mutex_clear(&e->mutex);
    g_mutex_clear(&e->callback_mutex);
    free(e);
}

dnsb_engine_config dnsb_engine_default_config(void) {
    dnsb_engine_config c = {
        .query_sets       = 50,    /* 1x baseline; UI offers multiplier */
        .spacing_ms       = 20,
        .timeout_ms       = 1500,
        .sideline_after   = 10,
        .concurrency      = 32,
        .probe_redirection = 1,
        .probe_dnssec      = 0,
    };
    return c;
}

void dnsb_engine_set_config(dnsb_engine *e, const dnsb_engine_config *cfg) {
    g_mutex_lock(&e->mutex);
    e->cfg = *cfg;
    g_mutex_unlock(&e->mutex);
}

dnsb_engine_config dnsb_engine_get_config(dnsb_engine *e) {
    g_mutex_lock(&e->mutex);
    dnsb_engine_config c = e->cfg;
    g_mutex_unlock(&e->mutex);
    return c;
}

void dnsb_engine_set_callback(dnsb_engine *e, dnsb_event_cb cb, void *user) {
    /* emit_event reads cb/cb_user under callback_mutex; writes must take it
       too, otherwise a worker can see a torn (half-updated) pointer pair. */
    g_mutex_lock(&e->callback_mutex);
    e->cb = cb;
    e->cb_user = user;
    g_mutex_unlock(&e->callback_mutex);
}

void dnsb_engine_set_domains(dnsb_engine *e, const char **domains, size_t n) {
    /* Workers read uncached_pool concurrently. Take the engine mutex so we
       don't free a buffer a worker is in the middle of indexing into. */
    g_mutex_lock(&e->mutex);
    for (size_t i = 0; i < e->uncached_pool_n; i++) free(e->uncached_pool[i]);
    free(e->uncached_pool);
    e->uncached_pool = calloc(n, sizeof(char *));
    e->uncached_pool_n = n;
    for (size_t i = 0; i < n; i++) e->uncached_pool[i] = dnsb_strdup(domains[i]);
    g_mutex_unlock(&e->mutex);
}

int dnsb_engine_add_resolver(dnsb_engine *e, dnsb_resolver *r) {
    dnsb_stats_init(&r->cached);
    dnsb_stats_init(&r->uncached);
    dnsb_stats_init(&r->dotcom);
    g_mutex_init(&r->stats_mutex);

    socklen_t sl = 0;
    int fam = dnsb_parse_addr(r->addr, r->port ? r->port : 53, &r->ep.sa, &sl);
    if (fam < 0) {
        DNSB_WARN("cannot parse resolver address: %s", r->addr);
        return -1;
    }
    r->ep.family  = fam;
    r->ep.sa_len  = sl;
    r->ep.port    = r->port ? r->port : 53;
    snprintf(r->ep.addr_str, sizeof(r->ep.addr_str), "%s", r->addr);

    resolver_vec_push(&e->resolvers, r);
    return 0;
}

size_t dnsb_engine_resolver_count(const dnsb_engine *e) { return e->resolvers.n; }
dnsb_resolver *dnsb_engine_resolver_at(dnsb_engine *e, size_t i) {
    if (i >= e->resolvers.n) return NULL;
    return e->resolvers.items[i];
}

int dnsb_engine_is_running(dnsb_engine *e) { return atomic_load(&e->running); }

static void emit_event(dnsb_engine *e, dnsb_event_kind kind, size_t idx, double progress) {
    if (!e->cb) return;
    dnsb_event evt = { .kind = kind, .resolver_index = idx, .progress = progress };
    g_mutex_lock(&e->callback_mutex);
    e->cb(&evt, e->cb_user);
    g_mutex_unlock(&e->callback_mutex);
}

static void make_random_label(char *out, size_t outlen) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i + 1 < outlen; i++) {
        out[i] = alphabet[g_random_int_range(0, (gint32)sizeof(alphabet) - 1)];
    }
    out[outlen - 1] = '\0';
}

typedef struct {
    dnsb_engine *eng;
    size_t       index;
} worker_arg;

/* Returns 0 if any valid DNS response (including NXDOMAIN) arrived in time,
   -1 on transport or parse error, -2 on timeout. */
static int do_one_query(dnsb_resolver *r, const char *qname, int timeout_ms, uint64_t *rtt_ns) {
    uint8_t qbuf[2048], rbuf[4096];
    size_t qlen = 0, rlen = 0;
    uint16_t id = (uint16_t)g_random_int();
    if (dnsb_pkt_build_query(qbuf, sizeof(qbuf), id, qname, DNSB_TYPE_A, 0, &qlen) != 0) return -1;

    int rc = -1;
    switch (r->transport) {
        case DNSB_TRANSPORT_UDP:
            rc = dnsb_udp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout_ms, rtt_ns);
            break;
        case DNSB_TRANSPORT_TCP:
            rc = dnsb_tcp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout_ms, rtt_ns);
            break;
        case DNSB_TRANSPORT_DOH:
            rc = dnsb_doh_exchange(&r->transport_state, &r->ep, r->hostname,
                                   qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout_ms, rtt_ns);
            break;
        case DNSB_TRANSPORT_DOT:
            rc = dnsb_dot_exchange(&r->transport_state, &r->ep, r->hostname,
                                   qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout_ms, rtt_ns);
            break;
    }
    if (rc != 0) return rc;

    if (rlen < 12) return -1;
    dnsb_response_info info;
    if (dnsb_pkt_parse_response(rbuf, rlen, &info) != 0) return -1;
    /* DoH and DoT may use their own IDs (some implementations rewrite); for
       UDP/TCP the ID must round-trip. */
    if (r->transport == DNSB_TRANSPORT_UDP || r->transport == DNSB_TRANSPORT_TCP) {
        if (info.id != id) return -1;
    }
    return 0;
}

static void worker_func(gpointer data, gpointer user_data) {
    worker_arg *w = data;
    dnsb_engine *e = w->eng;
    size_t idx = w->index;
    free(w);

    dnsb_resolver *r = e->resolvers.items[idx];
    int sets = e->cfg.query_sets;
    int spacing = e->cfg.spacing_ms;
    int timeout = e->cfg.timeout_ms;
    int sideline_after = e->cfg.sideline_after;

    /* Pick one popular domain to "warm" the cache so subsequent queries are
       cache hits. The very first query is the uncached/recursive timing. */
    const char *cached_domain = "google.com";

    /* First, single uncached query to warm cache and time the recursive path. */
    if (!atomic_load(&e->cancel)) {
        uint64_t rtt = 0;
        int rc = do_one_query(r, cached_domain, timeout, &rtt);
        g_mutex_lock(&r->stats_mutex);
        r->queries_sent++;
        if (rc == 0) {
            r->queries_ok++;
            r->consecutive_fails = 0;
            dnsb_stats_add(&r->uncached, dnsb_ns_to_ms(rtt));
        } else {
            r->consecutive_fails++;
        }
        g_mutex_unlock(&r->stats_mutex);
    }

    uint64_t last_emit = 0;

    for (int s = 0; s < sets && !atomic_load(&e->cancel); s++) {
        /* Cached: repeat the same warmed domain. */
        {
            uint64_t rtt = 0;
            int rc = do_one_query(r, cached_domain, timeout, &rtt);
            g_mutex_lock(&r->stats_mutex);
            r->queries_sent++;
            if (rc == 0) {
                r->queries_ok++;
                r->consecutive_fails = 0;
                dnsb_stats_add(&r->cached, dnsb_ns_to_ms(rtt));
            } else {
                r->consecutive_fails++;
            }
            g_mutex_unlock(&r->stats_mutex);
        }

        /* Uncached: random fresh subdomain under an uncached test domain.
           Snapshot a base name under the engine mutex so we don't race with
           dnsb_engine_set_domains freeing the pool out from under us. */
        char base_copy[256];
        base_copy[0] = '\0';
        g_mutex_lock(&e->mutex);
        if (e->uncached_pool_n > 0) {
            const char *base = e->uncached_pool[g_random_int_range(0, (gint32)e->uncached_pool_n)];
            if (base) snprintf(base_copy, sizeof(base_copy), "%s", base);
        }
        g_mutex_unlock(&e->mutex);
        if (base_copy[0]) {
            char label[10];
            make_random_label(label, sizeof(label));
            const char *base = base_copy;
            char fqdn[256];
            snprintf(fqdn, sizeof(fqdn), "%s.%s", label, base);
            uint64_t rtt = 0;
            int rc = do_one_query(r, fqdn, timeout, &rtt);
            g_mutex_lock(&r->stats_mutex);
            r->queries_sent++;
            if (rc == 0) {
                r->queries_ok++;
                r->consecutive_fails = 0;
                dnsb_stats_add(&r->uncached, dnsb_ns_to_ms(rtt));
            } else {
                r->consecutive_fails++;
            }
            g_mutex_unlock(&r->stats_mutex);
        }

        /* DotCom: random fresh <rand>.com. */
        {
            char label[10];
            make_random_label(label, sizeof(label));
            char fqdn[64];
            snprintf(fqdn, sizeof(fqdn), "%s.com", label);
            uint64_t rtt = 0;
            int rc = do_one_query(r, fqdn, timeout, &rtt);
            g_mutex_lock(&r->stats_mutex);
            r->queries_sent++;
            if (rc == 0) {
                r->queries_ok++;
                r->consecutive_fails = 0;
                dnsb_stats_add(&r->dotcom, dnsb_ns_to_ms(rtt));
            } else {
                r->consecutive_fails++;
            }
            g_mutex_unlock(&r->stats_mutex);
        }

        g_mutex_lock(&r->stats_mutex);
        int fails = r->consecutive_fails;
        if (fails >= sideline_after) r->sidelined = 1;
        g_mutex_unlock(&r->stats_mutex);
        if (fails >= sideline_after) break;

        uint64_t now = dnsb_now_ns();
        if (now - last_emit > EVENT_THROTTLE_NS) {
            last_emit = now;
            emit_event(e, DNSB_EVT_PROGRESS, idx, (double)(s + 1) / (double)sets);
        }
        g_usleep((gulong)spacing * 1000);
    }

    /* Redirection probe: ask for a random .invalid name. A well-behaved
       resolver returns NXDOMAIN; a redirector returns a synthetic A record. */
    if (e->cfg.probe_redirection && !atomic_load(&e->cancel)) {
        char label[16];
        make_random_label(label, sizeof(label));
        char fqdn[64];
        snprintf(fqdn, sizeof(fqdn), "%s.invalid", label);
        uint8_t qbuf[2048], rbuf[4096];
        size_t qlen = 0, rlen = 0;
        uint64_t rtt = 0;
        uint16_t id = (uint16_t)g_random_int();
        if (dnsb_pkt_build_query(qbuf, sizeof(qbuf), id, fqdn, DNSB_TYPE_A, 0, &qlen) == 0) {
            int rc = -1;
            switch (r->transport) {
                case DNSB_TRANSPORT_UDP: rc = dnsb_udp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_TCP: rc = dnsb_tcp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_DOH: rc = dnsb_doh_exchange(&r->transport_state, &r->ep, r->hostname, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_DOT: rc = dnsb_dot_exchange(&r->transport_state, &r->ep, r->hostname, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
            }
            if (rc == 0) {
                dnsb_response_info info;
                if (dnsb_pkt_parse_response(rbuf, rlen, &info) == 0) {
                    if (info.rcode == DNSB_RCODE_NOERROR && info.answer_family != 0) {
                        r->redirects = 1;
                    }
                }
            }
        }
    }

    /* DNSSEC probe: query a known signed name with the DO bit set; if the
       resolver returns AD=1 it is validating signatures. */
    if (e->cfg.probe_dnssec && !atomic_load(&e->cancel)) {
        const char *signed_name = "dnssec-tools.org";
        uint8_t qbuf[2048], rbuf[4096];
        size_t qlen = 0, rlen = 0;
        uint64_t rtt = 0;
        uint16_t id = (uint16_t)g_random_int();
        if (dnsb_pkt_build_query(qbuf, sizeof(qbuf), id, signed_name, DNSB_TYPE_A, 1, &qlen) == 0) {
            int rc = -1;
            switch (r->transport) {
                case DNSB_TRANSPORT_UDP: rc = dnsb_udp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_TCP: rc = dnsb_tcp_exchange(&r->ep, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_DOH: rc = dnsb_doh_exchange(&r->transport_state, &r->ep, r->hostname, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
                case DNSB_TRANSPORT_DOT: rc = dnsb_dot_exchange(&r->transport_state, &r->ep, r->hostname, qbuf, qlen, rbuf, sizeof(rbuf), &rlen, timeout, &rtt); break;
            }
            if (rc == 0) {
                dnsb_response_info info;
                if (dnsb_pkt_parse_response(rbuf, rlen, &info) == 0) {
                    if (info.rcode == DNSB_RCODE_NOERROR && info.ad) r->dnssec_ok = 1;
                }
            }
        }
    }

    emit_event(e, DNSB_EVT_RESOLVER_DONE, idx, 1.0);

    int rem = atomic_fetch_sub(&e->outstanding, 1) - 1;
    if (rem <= 0) {
        atomic_store(&e->running, 0);
        emit_event(e, DNSB_EVT_RUN_DONE, 0, 1.0);
    }
}

int dnsb_engine_start(dnsb_engine *e) {
    if (atomic_load(&e->running)) return -1;
    if (e->resolvers.n == 0) return -1;

    /* Reset all stats. */
    for (size_t i = 0; i < e->resolvers.n; i++) {
        dnsb_resolver *r = e->resolvers.items[i];
        dnsb_stats_reset(&r->cached);
        dnsb_stats_reset(&r->uncached);
        dnsb_stats_reset(&r->dotcom);
        r->queries_sent = 0;
        r->queries_ok = 0;
        r->consecutive_fails = 0;
        r->sidelined = 0;
        r->redirects = 0;
        r->dnssec_ok = 0;
    }

    atomic_store(&e->cancel, 0);
    atomic_store(&e->running, 1);
    atomic_store(&e->outstanding, (int)e->resolvers.n);

    if (e->pool) g_thread_pool_free(e->pool, FALSE, TRUE);
    e->pool = g_thread_pool_new(worker_func, e, e->cfg.concurrency, FALSE, NULL);

    for (size_t i = 0; i < e->resolvers.n; i++) {
        worker_arg *w = malloc(sizeof(*w));
        if (!w) {
            /* OOM: account for the resolver as "done" so outstanding lands
               at zero and the engine reports completion. */
            DNSB_WARN("OOM allocating worker_arg for resolver %zu; skipping", i);
            int rem = atomic_fetch_sub(&e->outstanding, 1) - 1;
            if (rem <= 0) {
                atomic_store(&e->running, 0);
                emit_event(e, DNSB_EVT_RUN_DONE, 0, 1.0);
            }
            continue;
        }
        w->eng = e;
        w->index = i;
        g_thread_pool_push(e->pool, w, NULL);
    }
    return 0;
}

void dnsb_engine_stop(dnsb_engine *e) {
    if (!e) return;
    atomic_store(&e->cancel, 1);
    if (e->pool) {
        g_thread_pool_free(e->pool, FALSE, TRUE);
        e->pool = NULL;
    }
    atomic_store(&e->running, 0);
}
