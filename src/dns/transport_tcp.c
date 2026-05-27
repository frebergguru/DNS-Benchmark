#include "transport.h"

#include "../net/time_ns.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define poll WSAPoll
#else
#include <poll.h>
#endif

static int wait_poll(dnsb_sock s, short events, int timeout_ms) {
    struct pollfd pfd = { .fd = s, .events = events, .revents = 0 };
    return poll(&pfd, 1, timeout_ms);
}

static int send_all(dnsb_sock s, const uint8_t *buf, size_t len, int timeout_ms, uint64_t deadline_ns) {
    size_t off = 0;
    while (off < len) {
        int r = (int)send(s, (const char *)(buf + off), (int)(len - off), 0);
        if (r > 0) { off += (size_t)r; continue; }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) return -1;
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINPROGRESS) return -1;
#endif
        int remain = timeout_ms;
        uint64_t now = dnsb_now_ns();
        if (now >= deadline_ns) return -2;
        remain = (int)((deadline_ns - now) / 1000000ULL);
        int pr = wait_poll(s, POLLOUT, remain);
        if (pr == 0) return -2;
        if (pr < 0)  return -1;
    }
    return 0;
}

static int recv_n(dnsb_sock s, uint8_t *buf, size_t need, uint64_t deadline_ns) {
    size_t off = 0;
    while (off < need) {
        uint64_t now = dnsb_now_ns();
        if (now >= deadline_ns) return -2;
        int remain = (int)((deadline_ns - now) / 1000000ULL);
        int pr = wait_poll(s, POLLIN, remain);
        if (pr == 0) return -2;
        if (pr < 0)  return -1;
        int r = (int)recv(s, (char *)(buf + off), (int)(need - off), 0);
        if (r > 0) { off += (size_t)r; continue; }
        if (r == 0) return -1;                              /* peer closed */
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) return -1;
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
#endif
    }
    return 0;
}

int dnsb_tcp_exchange(const dnsb_endpoint *ep,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns) {
    if (query_len > 65535) return -1;

    dnsb_sock s = socket(ep->family, SOCK_STREAM, 0);
    if (s == DNSB_INVALID_SOCK) return -1;
    if (dnsb_set_nonblocking(s) != 0) { dnsb_closesock(s); return -1; }

    uint64_t t0 = dnsb_now_ns();
    uint64_t deadline = t0 + (uint64_t)timeout_ms * 1000000ULL;

    int rc = connect(s, (const struct sockaddr *)&ep->sa, ep->sa_len);
    if (rc != 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        int inprog = (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS);
#else
        int inprog = (errno == EINPROGRESS);
#endif
        if (!inprog) { dnsb_closesock(s); return -1; }
        int pr = wait_poll(s, POLLOUT, timeout_ms);
        if (pr == 0) { dnsb_closesock(s); return -2; }
        if (pr < 0)  { dnsb_closesock(s); return -1; }
        int so_err = 0;
        socklen_t solen = sizeof(so_err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so_err, &solen);
        if (so_err != 0) { dnsb_closesock(s); return -1; }
    }

    uint8_t frame[2 + 65536];
    frame[0] = (uint8_t)(query_len >> 8);
    frame[1] = (uint8_t)(query_len & 0xff);
    memcpy(frame + 2, query, query_len);

    /* Preserve the -2/-1 distinction from send_all/recv_n so the engine
       can tell post-connect timeouts apart from hard failures; collapsing
       them caused slow resolvers to accumulate consecutive_fails at the
       same rate as broken ones and get sidelined too aggressively. */
    int sr = send_all(s, frame, 2 + query_len, timeout_ms, deadline);
    if (sr != 0) { dnsb_closesock(s); return sr; }

    uint8_t lenbuf[2];
    int rr = recv_n(s, lenbuf, 2, deadline);
    if (rr != 0) { dnsb_closesock(s); return rr; }
    size_t rlen = ((size_t)lenbuf[0] << 8) | lenbuf[1];
    if (rlen > resp_cap) { dnsb_closesock(s); return -1; }
    rr = recv_n(s, resp_buf, rlen, deadline);
    if (rr != 0) { dnsb_closesock(s); return rr; }

    *resp_len = rlen;
    *rtt_ns = dnsb_now_ns() - t0;
    dnsb_closesock(s);
    return 0;
}
