#include "resolvers_io.h"

#include "../util/log.h"
#include "../util/strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_transport(const char *s) {
    if (!s || dnsb_str_iequals(s, "udp")) return DNSB_TRANSPORT_UDP;
    if (dnsb_str_iequals(s, "tcp")) return DNSB_TRANSPORT_TCP;
    if (dnsb_str_iequals(s, "doh")) return DNSB_TRANSPORT_DOH;
    if (dnsb_str_iequals(s, "dot")) return DNSB_TRANSPORT_DOT;
    return -1;
}

static char *next_field(char **cursor) {
    char *p = *cursor;
    if (!p || !*p) return NULL;
    char *start = p;
    while (*p && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p) { *p = '\0'; p++; }
    *cursor = p;
    dnsb_strtrim(start);
    return start;
}

int dnsb_load_resolvers_tsv(const char *path, dnsb_engine *eng) {
    FILE *f = fopen(path, "rb");
    if (!f) { DNSB_WARN("cannot open resolver list %s", path); return -1; }

    char line[1024];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        char *cursor = p;
        char *name      = next_field(&cursor);
        char *owner     = next_field(&cursor);
        char *addr      = next_field(&cursor);
        char *transport = next_field(&cursor);
        char *port      = next_field(&cursor);
        char *hostname  = next_field(&cursor);

        if (!name || !addr) continue;
        int t = parse_transport(transport);
        if (t < 0) continue;

        dnsb_resolver *r = calloc(1, sizeof(*r));
        r->name      = dnsb_strdup(name);
        r->owner     = dnsb_strdup(owner ? owner : "");
        r->addr      = dnsb_strdup(addr);
        r->hostname  = (hostname && *hostname) ? dnsb_strdup(hostname) : NULL;
        r->transport = (dnsb_transport_kind)t;
        r->port      = port ? atoi(port) : 0;
        if (r->port == 0) {
            r->port = (t == DNSB_TRANSPORT_DOT) ? 853
                    : (t == DNSB_TRANSPORT_DOH) ? 443
                    : 53;
        }
        if (dnsb_engine_add_resolver(eng, r) != 0) {
            free(r->name); free(r->owner); free(r->addr); free(r->hostname); free(r);
            continue;
        }
        loaded++;
    }
    fclose(f);
    return loaded;
}

int dnsb_load_domains_txt(const char *path, char ***out_lines, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) { DNSB_WARN("cannot open domains list %s", path); return -1; }

    char  **arr = NULL;
    size_t  n   = 0, cap = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        dnsb_strtrim(p);
        if (!*p) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            arr = realloc(arr, cap * sizeof(char *));
        }
        arr[n++] = dnsb_strdup(p);
    }
    fclose(f);
    *out_lines = arr;
    *out_n = n;
    return (int)n;
}
