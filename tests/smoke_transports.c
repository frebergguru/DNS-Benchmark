/* Live transport smoke tests for TCP, DoH, and DoT against well-known
   public resolvers. Skipped via SKIP_NETWORK_TESTS=1. */
#include "dns/packet.h"
#include "dns/transport.h"
#include "net/socket_compat.h"
#include "net/time_ns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_ep(dnsb_endpoint *ep, const char *addr, int port) {
    memset(ep, 0, sizeof(*ep));
    int fam = dnsb_parse_addr(addr, port, &ep->sa, &ep->sa_len);
    ep->family = fam;
    ep->port = port;
    snprintf(ep->addr_str, sizeof(ep->addr_str), "%s", addr);
}

static int exchange_and_check(int rc, size_t rlen, const uint8_t *resp, uint16_t expect_id,
                              const char *label, uint64_t rtt) {
    if (rc == -2) { printf("  %-8s: TIMEOUT\n", label); return 0; }
    if (rc != 0)  { printf("  %-8s: error rc=%d\n", label, rc); return 0; }
    if (rlen < 12) { printf("  %-8s: short response\n", label); return 0; }
    dnsb_response_info info;
    if (dnsb_pkt_parse_response(resp, rlen, &info) != 0) {
        printf("  %-8s: parse failed\n", label); return 0;
    }
    printf("  %-8s: OK rtt=%.2f ms id=0x%04x rcode=%d ans=%d\n",
           label, dnsb_ns_to_ms(rtt), info.id, info.rcode, info.answers);
    return 1;
}

int main(void) {
    if (getenv("SKIP_NETWORK_TESTS")) {
        printf("smoke_transports: SKIP\n"); return 77;
    }
    if (dnsb_net_init() != 0) { fprintf(stderr, "net_init failed\n"); return 1; }

    int passes = 0;

    /* TCP via 1.1.1.1:53. */
    {
        dnsb_endpoint ep; fill_ep(&ep, "1.1.1.1", 53);
        uint8_t q[256], r[1500]; size_t qlen, rlen; uint64_t rtt = 0;
        if (dnsb_pkt_build_query(q, sizeof(q), 0xa11c, "cloudflare.com", DNSB_TYPE_A, 0, &qlen) != 0) return 1;
        int rc = dnsb_tcp_exchange(&ep, q, qlen, r, sizeof(r), &rlen, 3000, &rtt);
        passes += exchange_and_check(rc, rlen, r, 0xa11c, "TCP", rtt);
    }

    /* DoH via 1.1.1.1:443 with cloudflare-dns.com hostname. */
    {
        dnsb_endpoint ep; fill_ep(&ep, "1.1.1.1", 443);
        uint8_t q[256], r[4096]; size_t qlen, rlen; uint64_t rtt = 0;
        if (dnsb_pkt_build_query(q, sizeof(q), 0xd0fe, "cloudflare.com", DNSB_TYPE_A, 0, &qlen) != 0) return 1;
        void *state = NULL;
        int rc = dnsb_doh_exchange(&state, &ep, "cloudflare-dns.com",
                                   q, qlen, r, sizeof(r), &rlen, 5000, &rtt);
        passes += exchange_and_check(rc, rlen, r, 0xd0fe, "DoH", rtt);
        /* Reuse: second query should be faster due to connection reuse. */
        uint64_t rtt2 = 0;
        if (dnsb_pkt_build_query(q, sizeof(q), 0xd0ff, "example.org", DNSB_TYPE_A, 0, &qlen) == 0) {
            int rc2 = dnsb_doh_exchange(&state, &ep, "cloudflare-dns.com",
                                        q, qlen, r, sizeof(r), &rlen, 5000, &rtt2);
            passes += exchange_and_check(rc2, rlen, r, 0xd0ff, "DoH-2", rtt2);
        }
        dnsb_doh_free_state(state);
    }

    /* DoT via 1.1.1.1:853 with cloudflare-dns.com SNI. */
    {
        dnsb_endpoint ep; fill_ep(&ep, "1.1.1.1", 853);
        uint8_t q[256], r[4096]; size_t qlen, rlen; uint64_t rtt = 0;
        if (dnsb_pkt_build_query(q, sizeof(q), 0xd07e, "cloudflare.com", DNSB_TYPE_A, 0, &qlen) != 0) return 1;
        void *state = NULL;
        int rc = dnsb_dot_exchange(&state, &ep, "cloudflare-dns.com",
                                   q, qlen, r, sizeof(r), &rlen, 5000, &rtt);
        passes += exchange_and_check(rc, rlen, r, 0xd07e, "DoT", rtt);
        /* Second query should benefit from TLS session resumption. */
        uint64_t rtt2 = 0;
        if (dnsb_pkt_build_query(q, sizeof(q), 0xd07f, "example.org", DNSB_TYPE_A, 0, &qlen) == 0) {
            int rc2 = dnsb_dot_exchange(&state, &ep, "cloudflare-dns.com",
                                        q, qlen, r, sizeof(r), &rlen, 5000, &rtt2);
            passes += exchange_and_check(rc2, rlen, r, 0xd07f, "DoT-2", rtt2);
        }
        dnsb_dot_free_state(state);
    }

    dnsb_net_shutdown();
    printf("smoke_transports: %d/5 OK\n", passes);
    return passes == 5 ? 0 : 1;
}
