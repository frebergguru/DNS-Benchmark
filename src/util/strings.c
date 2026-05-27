#include "strings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *dnsb_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

char *dnsb_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t k = 0;
    while (k < n && s[k]) k++;
    char *r = malloc(k + 1);
    if (!r) return NULL;
    memcpy(r, s, k);
    r[k] = '\0';
    return r;
}

void dnsb_strtrim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

int dnsb_str_iequals(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}
