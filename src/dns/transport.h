#ifndef DNSB_DNS_TRANSPORT_H
#define DNSB_DNS_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "../net/socket_compat.h"

typedef enum {
    DNSB_TRANSPORT_UDP = 0,
    DNSB_TRANSPORT_TCP,
    DNSB_TRANSPORT_DOH,
    DNSB_TRANSPORT_DOT,
} dnsb_transport_kind;

typedef struct {
    int family;                /* AF_INET / AF_INET6 */
    struct sockaddr_storage sa;
    socklen_t                sa_len;
    int                      port;
    char addr_str[64];
} dnsb_endpoint;

/* Plain UDP exchange. */
int dnsb_udp_exchange(const dnsb_endpoint *ep,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns);

/* Plain TCP exchange (DNS-over-TCP framing: 2-byte length prefix). */
int dnsb_tcp_exchange(const dnsb_endpoint *ep,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns);

/* DNS-over-HTTPS (RFC 8484, POST application/dns-message).
   host is used to build the URL: https://<host>/dns-query.
   The endpoint sa is used via CONNECT_TO so the request hits the specific IP. */
int dnsb_doh_exchange(void **state_inout,
                      const dnsb_endpoint *ep, const char *host,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns);

/* DNS-over-TLS (RFC 7858). state caches an SSL_CTX for session resumption.
   sni is the hostname presented to TLS (defaults to addr_str when NULL). */
int dnsb_dot_exchange(void **state_inout,
                      const dnsb_endpoint *ep, const char *sni,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns);

void dnsb_doh_free_state(void *state);
void dnsb_dot_free_state(void *state);

#endif
