#include "dns/packet.h"
#include "net/socket_compat.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_build_query_basic(void) {
    uint8_t buf[512];
    size_t  len = 0;
    int rc = dnsb_pkt_build_query(buf, sizeof(buf), 0x1234, "example.com", DNSB_TYPE_A, 0, &len);
    assert(rc == 0);
    /* Header (12) + 7 example + 3 com + null + qtype + qclass = 12 + 7+1+3+1+1 + 4 = 29 */
    assert(len == 29);

    /* ID == 0x1234 */
    assert(buf[0] == 0x12 && buf[1] == 0x34);
    /* QR=0, OPCODE=0, RD=1 */
    assert(buf[2] == 0x01 && buf[3] == 0x00);
    /* QDCOUNT=1 */
    assert(buf[4] == 0 && buf[5] == 1);
    /* AR count = 0 (no EDNS) */
    assert(buf[10] == 0 && buf[11] == 0);

    /* Labels: 7 'example' 3 'com' 0 */
    assert(buf[12] == 7);
    assert(memcmp(buf + 13, "example", 7) == 0);
    assert(buf[20] == 3);
    assert(memcmp(buf + 21, "com", 3) == 0);
    assert(buf[24] == 0);
    /* qtype A */
    assert(buf[25] == 0 && buf[26] == DNSB_TYPE_A);
    /* qclass IN */
    assert(buf[27] == 0 && buf[28] == 1);
}

static void test_build_query_edns(void) {
    uint8_t buf[512];
    size_t  len = 0;
    int rc = dnsb_pkt_build_query(buf, sizeof(buf), 1, "a.b", DNSB_TYPE_AAAA, 1, &len);
    assert(rc == 0);
    /* AR count = 1 (OPT RR present) */
    assert(buf[11] == 1);
    /* OPT TTL byte for DO bit = 0x80 */
    /* Find OPT by walking past the question. We know layout: header(12) + name(1+1+1+1+0=5) + 4(type/class) = 21 → OPT at 21 */
    size_t opt = 12 + 1 + 1 + 1 + 1 + 1 + 4;
    /* root name(1) type(2) class(2) ttl(4) rdlen(2) = 11 */
    assert(buf[opt] == 0);
    assert(buf[opt + 1] == 0 && buf[opt + 2] == DNSB_TYPE_OPT);
    assert(buf[opt + 7] == 0x80); /* DO high bit in TTL byte 3 */
}

static void test_parse_minimal_response(void) {
    /* Craft a minimal response: ID 0x4242, QR=1 RD=1 RA=1, RCODE=0,
       qdcount=1, ancount=0, question for "x.com" type A. */
    uint8_t buf[] = {
        0x42, 0x42,
        0x81, 0x80,        /* QR=1, RD=1, RA=1, rcode=0 */
        0x00, 0x01,        /* qdcount=1 */
        0x00, 0x00,        /* ancount=0 */
        0x00, 0x00,        /* nscount=0 */
        0x00, 0x00,        /* arcount=0 */
        0x01, 'x',
        0x03, 'c', 'o', 'm',
        0x00,
        0x00, 0x01,        /* qtype A */
        0x00, 0x01,        /* qclass IN */
    };
    dnsb_response_info info;
    int rc = dnsb_pkt_parse_response(buf, sizeof(buf), &info);
    assert(rc == 0);
    assert(info.id == 0x4242);
    assert(info.rcode == 0);
    assert(info.recursion_available == 1);
    assert(info.answers == 0);
    assert(info.answer_family == 0);
}

static void test_parse_response_with_a_record(void) {
    /* Same as above but with one A answer 93.184.216.34. */
    uint8_t buf[] = {
        0x42, 0x42,
        0x81, 0x80,
        0x00, 0x01,
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,
        0x01, 'x',
        0x03, 'c', 'o', 'm',
        0x00,
        0x00, 0x01, 0x00, 0x01,
        /* Answer: name as a compression pointer to offset 12 */
        0xc0, 0x0c,
        0x00, 0x01, 0x00, 0x01,             /* type A class IN */
        0x00, 0x00, 0x01, 0x2c,             /* TTL 300 */
        0x00, 0x04,                          /* rdlen 4 */
        93, 184, 216, 34,
    };
    dnsb_response_info info;
    int rc = dnsb_pkt_parse_response(buf, sizeof(buf), &info);
    assert(rc == 0);
    assert(info.answers == 1);
    assert(info.answer_family == AF_INET);
    assert(info.answer_addr[0] == 93 && info.answer_addr[1] == 184 &&
           info.answer_addr[2] == 216 && info.answer_addr[3] == 34);
}

static void test_parse_rejects_too_short(void) {
    uint8_t buf[5] = { 0 };
    dnsb_response_info info;
    int rc = dnsb_pkt_parse_response(buf, sizeof(buf), &info);
    assert(rc != 0);
}

int main(void) {
    test_build_query_basic();
    test_build_query_edns();
    test_parse_minimal_response();
    test_parse_response_with_a_record();
    test_parse_rejects_too_short();
    printf("test_dns_packet: OK\n");
    return 0;
}
