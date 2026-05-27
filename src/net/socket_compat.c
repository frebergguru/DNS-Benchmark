#include "socket_compat.h"

#include <string.h>

int dnsb_net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void dnsb_net_shutdown(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int dnsb_set_nonblocking(dnsb_sock s) {
#ifdef _WIN32
    u_long m = 1;
    return ioctlsocket(s, FIONBIO, &m) == 0 ? 0 : -1;
#else
    int f = fcntl(s, F_GETFL, 0);
    if (f < 0) return -1;
    return fcntl(s, F_SETFL, f | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

int dnsb_parse_addr(const char *addr, int port, struct sockaddr_storage *out, socklen_t *outlen) {
    memset(out, 0, sizeof(*out));
    struct sockaddr_in *v4 = (struct sockaddr_in *)out;
    if (inet_pton(AF_INET, addr, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons((unsigned short)port);
        *outlen = sizeof(*v4);
        return AF_INET;
    }
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)out;
    if (inet_pton(AF_INET6, addr, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons((unsigned short)port);
        *outlen = sizeof(*v6);
        return AF_INET6;
    }
    return -1;
}
