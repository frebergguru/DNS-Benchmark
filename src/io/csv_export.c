#include "csv_export.h"

#include <stdio.h>
#include <string.h>

static void write_csv_field(FILE *f, const char *s) {
    if (!s) { fputs("", f); return; }
    int needs_quote = 0;
    for (const char *p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { needs_quote = 1; break; }
    }
    if (!needs_quote) { fputs(s, f); return; }
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

int dnsb_csv_export(const char *path, dnsb_engine *eng) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fputs("name,owner,address,transport,port,"
          "cached_min_ms,cached_avg_ms,cached_median_ms,cached_max_ms,cached_stddev_ms,"
          "uncached_min_ms,uncached_avg_ms,uncached_median_ms,uncached_max_ms,uncached_stddev_ms,"
          "dotcom_min_ms,dotcom_avg_ms,dotcom_median_ms,dotcom_max_ms,dotcom_stddev_ms,"
          "queries_sent,queries_ok,reliability_pct,redirects,dnssec_ok,sidelined\n", f);

    size_t n = dnsb_engine_resolver_count(eng);
    for (size_t i = 0; i < n; i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(eng, i);
        const char *transport =
            r->transport == DNSB_TRANSPORT_UDP ? "udp" :
            r->transport == DNSB_TRANSPORT_TCP ? "tcp" :
            r->transport == DNSB_TRANSPORT_DOH ? "doh" : "dot";

        write_csv_field(f, r->name);  fputc(',', f);
        write_csv_field(f, r->owner); fputc(',', f);
        write_csv_field(f, r->addr);  fputc(',', f);
        fputs(transport, f);          fputc(',', f);
        fprintf(f, "%d,", r->port);

        /* Snapshot every numeric stat under the resolver lock so a still-
           running worker (if any) doesn't realloc the samples buffer while
           we sort it for the median. */
        g_mutex_lock(&r->stats_mutex);
        double c_min = r->cached.min_ms,  c_max = r->cached.max_ms;
        double u_min = r->uncached.min_ms, u_max = r->uncached.max_ms;
        double d_min = r->dotcom.min_ms,   d_max = r->dotcom.max_ms;
        double c_mean = dnsb_stats_mean(&r->cached);
        double u_mean = dnsb_stats_mean(&r->uncached);
        double d_mean = dnsb_stats_mean(&r->dotcom);
        double c_med  = dnsb_stats_median(&r->cached);
        double u_med  = dnsb_stats_median(&r->uncached);
        double d_med  = dnsb_stats_median(&r->dotcom);
        double c_sd   = dnsb_stats_stddev(&r->cached);
        double u_sd   = dnsb_stats_stddev(&r->uncached);
        double d_sd   = dnsb_stats_stddev(&r->dotcom);
        int    sent = r->queries_sent, okq = r->queries_ok;
        int    redir = r->redirects, dnssec = r->dnssec_ok, sidel = r->sidelined;
        g_mutex_unlock(&r->stats_mutex);

        double reliab = sent ? 100.0 * (double)okq / (double)sent : 0.0;
        fprintf(f, "%.3f,%.3f,%.3f,%.3f,%.3f,", c_min, c_mean, c_med, c_max, c_sd);
        fprintf(f, "%.3f,%.3f,%.3f,%.3f,%.3f,", u_min, u_mean, u_med, u_max, u_sd);
        fprintf(f, "%.3f,%.3f,%.3f,%.3f,%.3f,", d_min, d_mean, d_med, d_max, d_sd);
        fprintf(f, "%d,%d,%.2f,%d,%d,%d\n",
                sent, okq, reliab, redir, dnssec, sidel);
    }
    fclose(f);
    return 0;
}
