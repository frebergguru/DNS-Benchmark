#ifndef DNSB_ENGINE_H
#define DNSB_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include <glib.h>

#include "stats.h"
#include "../dns/transport.h"

typedef struct {
    /* Configuration (set before adding to engine). */
    char *name;
    char *owner;
    char *addr;                 /* presentation form */
    char *hostname;             /* optional: SNI / DoH URL host (defaults to addr) */
    int   port;
    dnsb_transport_kind transport;

    /* Resolved endpoint. */
    dnsb_endpoint ep;

    /* Runtime flags. */
    int   pinned;
    int   system_configured;
    int   sidelined;
    int   redirects;            /* set after redirection probe */
    int   dnssec_ok;            /* set after DNSSEC probe */

    /* Per-class stats. */
    dnsb_stats cached;
    dnsb_stats uncached;
    dnsb_stats dotcom;

    int queries_sent;
    int queries_ok;
    int consecutive_fails;

    /* Protects all of {cached, uncached, dotcom, queries_sent, queries_ok,
       consecutive_fails, sidelined, redirects, dnssec_ok} from torn reads
       and from realloc-during-qsort. Worker writes, UI reads under lock. */
    GMutex stats_mutex;

    /* Opaque per-resolver transport state (e.g. cached TLS session). */
    void *transport_state;
} dnsb_resolver;

typedef struct {
    int query_sets;            /* total query-sets per resolver (default 250) */
    int spacing_ms;            /* sleep between query-sets */
    int timeout_ms;            /* per-query timeout */
    int sideline_after;        /* consecutive fails before sidelining */
    int concurrency;           /* max worker threads */
    int probe_redirection;     /* run NXDOMAIN probe at end */
    int probe_dnssec;          /* run DNSSEC probe */
} dnsb_engine_config;

typedef enum {
    DNSB_EVT_PROGRESS = 1,     /* one resolver's stats updated */
    DNSB_EVT_RESOLVER_DONE,    /* resolver finished its query-sets */
    DNSB_EVT_RUN_DONE,         /* all resolvers complete */
} dnsb_event_kind;

typedef struct {
    dnsb_event_kind kind;
    size_t resolver_index;     /* index into the resolver array */
    double progress;           /* 0.0..1.0 for PROGRESS */
} dnsb_event;

typedef void (*dnsb_event_cb)(const dnsb_event *evt, void *user);

typedef struct dnsb_engine dnsb_engine;

dnsb_engine *dnsb_engine_new(void);
void         dnsb_engine_free(dnsb_engine *eng);

void         dnsb_engine_set_config(dnsb_engine *eng, const dnsb_engine_config *cfg);
void         dnsb_engine_set_callback(dnsb_engine *eng, dnsb_event_cb cb, void *user);
void         dnsb_engine_set_domains(dnsb_engine *eng, const char **domains, size_t n);

/* Engine takes ownership of the resolver array contents; caller still owns the array slot. */
int          dnsb_engine_add_resolver(dnsb_engine *eng, dnsb_resolver *r);
size_t       dnsb_engine_resolver_count(const dnsb_engine *eng);
dnsb_resolver *dnsb_engine_resolver_at(dnsb_engine *eng, size_t i);

int          dnsb_engine_start(dnsb_engine *eng);
void         dnsb_engine_stop(dnsb_engine *eng);
int          dnsb_engine_is_running(dnsb_engine *eng);

dnsb_engine_config dnsb_engine_default_config(void);
dnsb_engine_config dnsb_engine_get_config(dnsb_engine *eng);

#endif
