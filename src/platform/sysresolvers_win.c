#include "sysresolvers.h"

#include "../util/strings.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <stdlib.h>
#include <string.h>

int dnsb_get_system_resolvers(char ***out_list, size_t *n) {
    *out_list = NULL;
    *n = 0;

    ULONG buflen = 16 * 1024;
    IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES *)malloc(buflen);
    if (!addrs) return -1;

    ULONG rc = GetAdaptersAddresses(AF_UNSPEC,
                                    GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                    NULL, addrs, &buflen);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        free(addrs);
        addrs = (IP_ADAPTER_ADDRESSES *)malloc(buflen);
        if (!addrs) return -1;
        rc = GetAdaptersAddresses(AF_UNSPEC,
                                  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                  NULL, addrs, &buflen);
    }
    if (rc != ERROR_SUCCESS) { free(addrs); return -1; }

    char **arr = NULL;
    size_t cap = 0, count = 0;
    for (IP_ADAPTER_ADDRESSES *a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (IP_ADAPTER_DNS_SERVER_ADDRESS *d = a->FirstDnsServerAddress; d; d = d->Next) {
            char buf[INET6_ADDRSTRLEN] = {0};
            const struct sockaddr *sa = d->Address.lpSockaddr;
            if (sa->sa_family == AF_INET) {
                const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
                inet_ntop(AF_INET, &s4->sin_addr, buf, sizeof(buf));
            } else if (sa->sa_family == AF_INET6) {
                const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
                inet_ntop(AF_INET6, &s6->sin6_addr, buf, sizeof(buf));
            } else {
                continue;
            }
            if (!buf[0]) continue;
            if (count == cap) {
                cap = cap ? cap * 2 : 4;
                arr = realloc(arr, (cap + 1) * sizeof(char *));
            }
            arr[count++] = dnsb_strdup(buf);
        }
    }
    if (arr) arr[count] = NULL;
    *out_list = arr;
    *n = count;
    free(addrs);
    return 0;
}
