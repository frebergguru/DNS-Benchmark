#include "sysresolvers.h"

#include "../util/strings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dnsb_get_system_resolvers(char ***out_list, size_t *n) {
    *out_list = NULL;
    *n = 0;

    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) return -1;

    char **arr = NULL;
    size_t cap = 0, count = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "nameserver", 10) != 0) continue;
        /* The keyword must be followed by whitespace or end-of-line.
           Without this check we'd also match e.g. "nameserverlocal". */
        if (p[10] != '\0' && !isspace((unsigned char)p[10])) continue;
        p += 10;
        while (isspace((unsigned char)*p)) p++;
        char *end = p;
        while (*end && !isspace((unsigned char)*end) && *end != '#') end++;
        *end = '\0';
        if (!*p) continue;
        if (count == cap) {
            size_t newcap = cap ? cap * 2 : 4;
            /* +1 for the NULL terminator slot used by callers iterating
               until arr[k] == NULL. */
            char **tmp = realloc(arr, (newcap + 1) * sizeof(char *));
            if (!tmp) break;
            arr = tmp;
            cap = newcap;
        }
        arr[count++] = dnsb_strdup(p);
    }
    fclose(f);
    if (arr) arr[count] = NULL;
    *out_list = arr;
    *n = count;
    return 0;
}
