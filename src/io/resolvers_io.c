#include "resolvers_io.h"

#include "../util/log.h"
#include "../util/strings.h"

#include <errno.h>
#include <limits.h>
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
    if (*p) {
        /* Consume CRLF as a single terminator so a Windows-edited TSV
           doesn't yield a phantom empty field that confuses callers. */
        char term = *p;
        *p++ = '\0';
        if (term == '\r' && *p == '\n') p++;
    }
    *cursor = p;
    dnsb_strtrim(start);
    return start;
}

/* Parse a port in [1, 65535]. Returns 0 on any kind of malformed input
   (caller substitutes the protocol default). */
static int parse_port(const char *s) {
    if (!s || !*s) return 0;
    errno = 0;
    char *endp = NULL;
    long v = strtol(s, &endp, 10);
    if (errno != 0 || endp == s || (endp && *endp != '\0')) return 0;
    if (v < 1 || v > 65535) return 0;
    return (int)v;
}

int dnsb_load_resolvers_tsv(const char *path, dnsb_engine *eng) {
    FILE *f = fopen(path, "rb");
    if (!f) { DNSB_WARN("cannot open resolver list %s", path); return -1; }

    char line[1024];
    int loaded = 0;
    int first_line = 1;
    while (fgets(line, sizeof(line), f)) {
        size_t llen = strlen(line);
        /* Detect silent truncation: fgets fills the buffer but doesn't end
           in a newline. Skip the rest of the line so it can't bleed into
           the next iteration. */
        int truncated = (llen == sizeof(line) - 1 && line[llen - 1] != '\n');
        if (truncated) {
            DNSB_WARN("resolver line in %s exceeds %zu bytes; skipping", path, sizeof(line) - 1);
            int ch;
            do { ch = fgetc(f); } while (ch != EOF && ch != '\n');
            continue;
        }

        char *p = line;
        /* Strip a UTF-8 BOM if it's the first three bytes of the file. */
        if (first_line) {
            first_line = 0;
            if ((unsigned char)p[0] == 0xEF &&
                (unsigned char)p[1] == 0xBB &&
                (unsigned char)p[2] == 0xBF) {
                p += 3;
            }
        }
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
        if (!r) { DNSB_WARN("OOM allocating resolver"); continue; }
        r->name      = dnsb_strdup(name);
        r->owner     = dnsb_strdup(owner ? owner : "");
        r->addr      = dnsb_strdup(addr);
        r->hostname  = (hostname && *hostname) ? dnsb_strdup(hostname) : NULL;
        r->transport = (dnsb_transport_kind)t;
        r->port      = parse_port(port);
        if (port && *port && r->port == 0) {
            DNSB_WARN("invalid port '%s' for resolver %s; using protocol default", port, name);
        }
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
        size_t llen = strlen(line);
        if (llen == sizeof(line) - 1 && line[llen - 1] != '\n') {
            DNSB_WARN("domain line in %s exceeds %zu bytes; skipping", path, sizeof(line) - 1);
            int ch; do { ch = fgetc(f); } while (ch != EOF && ch != '\n');
            continue;
        }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        dnsb_strtrim(p);
        if (!*p) continue;
        if (n == cap) {
            size_t newcap = cap ? cap * 2 : 16;
            char **tmp = realloc(arr, newcap * sizeof(char *));
            if (!tmp) {
                DNSB_WARN("OOM growing domain list; aborting load at %zu entries", n);
                break;
            }
            arr = tmp;
            cap = newcap;
        }
        arr[n++] = dnsb_strdup(p);
    }
    fclose(f);
    *out_lines = arr;
    *out_n = n;
    return (int)n;
}
