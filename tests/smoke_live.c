/* Live UDP exchange smoke check. Skips silently if SKIP_NETWORK_TESTS is set.
   Sends a single A query to 1.1.1.1, expects a valid response in under 2s. */
#include "dns/packet.h"
#include "dns/transport.h"
#include "net/socket_compat.h"
#include "net/time_ns.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    if (getenv("SKIP_NETWORK_TESTS")) {
        printf("smoke_live: SKIP (SKIP_NETWORK_TESTS set)\n");
        return 77;
    }
    if (dnsb_net_init() != 0) { fprintf(stderr, "net_init failed\n"); return 1; }

    dnsb_endpoint ep = {0};
    int fam = dnsb_parse_addr("1.1.1.1", 53, &ep.sa, &ep.sa_len);
    assert(fam == AF_INET);
    ep.family = fam;
    ep.port = 53;

    uint8_t q[256], r[1500];
    size_t qlen = 0, rlen = 0;
    int rc = dnsb_pkt_build_query(q, sizeof(q), 0xbeef, "cloudflare.com", DNSB_TYPE_A, 0, &qlen);
    assert(rc == 0);

    uint64_t rtt = 0;
    rc = dnsb_udp_exchange(&ep, q, qlen, r, sizeof(r), &rlen, 2000, &rtt);
    if (rc == -2) { printf("smoke_live: TIMEOUT\n"); return 1; }
    if (rc != 0)  { printf("smoke_live: socket error\n"); return 1; }

    dnsb_response_info info;
    rc = dnsb_pkt_parse_response(r, rlen, &info);
    if (rc != 0) { printf("smoke_live: parse failed\n"); return 1; }
    if (info.id != 0xbeef) { printf("smoke_live: id mismatch\n"); return 1; }

    printf("smoke_live: OK rtt=%.2f ms rcode=%d answers=%d\n",
           dnsb_ns_to_ms(rtt), info.rcode, info.answers);

    dnsb_net_shutdown();
    return 0;
}
