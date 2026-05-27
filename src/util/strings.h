#ifndef DNSB_UTIL_STRINGS_H
#define DNSB_UTIL_STRINGS_H

#include <stddef.h>

char *dnsb_strdup(const char *s);
char *dnsb_strndup(const char *s, size_t n);
void  dnsb_strtrim(char *s);
int   dnsb_str_iequals(const char *a, const char *b);

#endif
