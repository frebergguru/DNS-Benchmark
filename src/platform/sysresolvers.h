#ifndef DNSB_SYSRESOLVERS_H
#define DNSB_SYSRESOLVERS_H

#include <stddef.h>

/* Return a NULL-terminated array of strdup'd IP strings for the system's
   currently-configured DNS resolvers. Caller frees each entry and the array.
   Out parameter *n receives the count (also discoverable by the NULL terminator). */
int dnsb_get_system_resolvers(char ***out_list, size_t *n);

#endif
