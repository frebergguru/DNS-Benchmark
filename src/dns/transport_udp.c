#include "transport.h"

#include "../net/time_ns.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define poll WSAPoll
#else
#include <poll.h>
#endif

int dnsb_udp_exchange(const dnsb_endpoint *ep,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns) {
    dnsb_sock s = socket(ep->family, SOCK_DGRAM, 0);
    if (s == DNSB_INVALID_SOCK) return -1;

    if (dnsb_set_nonblocking(s) != 0) { dnsb_closesock(s); return -1; }

    uint64_t t0 = dnsb_now_ns();

    int sent = sendto(s, (const char *)query, (int)query_len, 0,
                      (const struct sockaddr *)&ep->sa, ep->sa_len);
    if (sent < 0 || (size_t)sent != query_len) { dnsb_closesock(s); return -1; }

    struct pollfd pfd;
    pfd.fd = s;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr == 0)  { dnsb_closesock(s); return -2; }
    if (pr  < 0)  { dnsb_closesock(s); return -1; }

    struct sockaddr_storage from;
    socklen_t fromlen = sizeof(from);
    int n = recvfrom(s, (char *)resp_buf, (int)resp_cap, 0,
                     (struct sockaddr *)&from, &fromlen);
    if (n < 0) { dnsb_closesock(s); return -1; }

    uint64_t t1 = dnsb_now_ns();
    *resp_len = (size_t)n;
    *rtt_ns = t1 - t0;

    dnsb_closesock(s);
    return 0;
}
