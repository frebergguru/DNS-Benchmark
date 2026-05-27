#ifndef DNSB_DNS_PACKET_H
#define DNSB_DNS_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* Common RR types. */
#define DNSB_TYPE_A     1
#define DNSB_TYPE_NS    2
#define DNSB_TYPE_CNAME 5
#define DNSB_TYPE_TXT   16
#define DNSB_TYPE_AAAA  28
#define DNSB_TYPE_OPT   41

/* Response codes. */
#define DNSB_RCODE_NOERROR  0
#define DNSB_RCODE_FORMERR  1
#define DNSB_RCODE_SERVFAIL 2
#define DNSB_RCODE_NXDOMAIN 3
#define DNSB_RCODE_NOTIMP   4
#define DNSB_RCODE_REFUSED  5

#define DNSB_FLAG_QR  0x8000
#define DNSB_FLAG_AA  0x0400
#define DNSB_FLAG_TC  0x0200
#define DNSB_FLAG_RD  0x0100
#define DNSB_FLAG_RA  0x0080
#define DNSB_FLAG_AD  0x0020
#define DNSB_FLAG_CD  0x0010

typedef struct {
    uint16_t id;
    int rcode;
    int truncated;
    int ad;
    int recursion_available;
    int answers;
    int authorities;
    int additional;
    int answer_family;            /* AF_INET / AF_INET6 / 0 */
    uint8_t  answer_addr[16];
} dnsb_response_info;

/* Build a wire-format DNS query into buf.
   qname is a dotted name like "google.com".
   If edns_do != 0, append an OPT RR with the DO bit set (4096-byte UDP payload).
   On success returns 0 and writes the encoded length to *out_len. */
int dnsb_pkt_build_query(uint8_t *buf, size_t buflen,
                         uint16_t id, const char *qname, uint16_t qtype,
                         int edns_do, size_t *out_len);

/* Parse a wire-format DNS response. Fills out info; returns 0 on success.
   On malformed input returns -1 (info contents undefined). */
int dnsb_pkt_parse_response(const uint8_t *buf, size_t buflen, dnsb_response_info *out);

#endif
