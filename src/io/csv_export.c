#include "csv_export.h"

#include <glib.h>

#include <stdio.h>
#include <string.h>

static int needs_quoting(const char *s) {
    if (!s || !*s) return 0;
    /* Defeat spreadsheet formula injection (CWE-1236): cells beginning
       with these characters are interpreted as formulas by Excel and
       LibreOffice Calc. Quoting alone is not enough — Excel still strips
       outer quotes — but at least the leading quote breaks the formula. */
    if (*s == '=' || *s == '+' || *s == '-' || *s == '@') return 1;
    for (const char *p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r' || *p == '\t') return 1;
    }
    return 0;
}

static void write_csv_field(FILE *f, const char *s) {
    if (!s) return;
    if (!needs_quoting(s)) { fputs(s, f); return; }
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

/* Write a double with a fixed decimal point regardless of the user's
   LC_NUMERIC. fprintf("%.3f", ...) writes a comma in many locales, which
   would corrupt a comma-separated file. */
static void write_csv_double(FILE *f, double v, int prec) {
    char fmt[8];
    char buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", prec);
    g_ascii_formatd(buf, sizeof(buf), fmt, v);
    fputs(buf, f);
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
        write_csv_double(f, c_min, 3); fputc(',', f);
        write_csv_double(f, c_mean, 3); fputc(',', f);
        write_csv_double(f, c_med, 3); fputc(',', f);
        write_csv_double(f, c_max, 3); fputc(',', f);
        write_csv_double(f, c_sd, 3); fputc(',', f);
        write_csv_double(f, u_min, 3); fputc(',', f);
        write_csv_double(f, u_mean, 3); fputc(',', f);
        write_csv_double(f, u_med, 3); fputc(',', f);
        write_csv_double(f, u_max, 3); fputc(',', f);
        write_csv_double(f, u_sd, 3); fputc(',', f);
        write_csv_double(f, d_min, 3); fputc(',', f);
        write_csv_double(f, d_mean, 3); fputc(',', f);
        write_csv_double(f, d_med, 3); fputc(',', f);
        write_csv_double(f, d_max, 3); fputc(',', f);
        write_csv_double(f, d_sd, 3); fputc(',', f);
        fprintf(f, "%d,%d,", sent, okq);
        write_csv_double(f, reliab, 2);
        fprintf(f, ",%d,%d,%d\n", redir, dnssec, sidel);
    }
    fclose(f);
    return 0;
}
