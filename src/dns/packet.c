#include "packet.h"

#include "../net/socket_compat.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
static uint16_t get_u16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int encode_qname(uint8_t *buf, size_t buflen, size_t off, const char *qname, size_t *new_off) {
    const char *p = qname;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t lab_len = dot ? (size_t)(dot - p) : strlen(p);
        if (lab_len == 0 || lab_len > 63) return -1;
        if (off + 1 + lab_len > buflen) return -1;
        buf[off++] = (uint8_t)lab_len;
        memcpy(buf + off, p, lab_len);
        off += lab_len;
        p += lab_len;
        if (*p == '.') p++;
    }
    if (off + 1 > buflen) return -1;
    buf[off++] = 0;
    *new_off = off;
    return 0;
}

int dnsb_pkt_build_query(uint8_t *buf, size_t buflen,
                         uint16_t id, const char *qname, uint16_t qtype,
                         int edns_do, size_t *out_len) {
    if (buflen < 12) return -1;
    memset(buf, 0, 12);
    put_u16(buf + 0, id);
    put_u16(buf + 2, DNSB_FLAG_RD);
    put_u16(buf + 4, 1);
    put_u16(buf + 6, 0);
    put_u16(buf + 8, 0);
    put_u16(buf + 10, edns_do ? 1 : 0);

    size_t off = 12;
    if (encode_qname(buf, buflen, off, qname, &off) != 0) return -1;
    if (off + 4 > buflen) return -1;
    put_u16(buf + off, qtype); off += 2;
    put_u16(buf + off, 1);     off += 2;     /* IN */

    if (edns_do) {
        if (off + 11 > buflen) return -1;
        buf[off++] = 0;                       /* root name */
        put_u16(buf + off, DNSB_TYPE_OPT); off += 2;
        put_u16(buf + off, 4096); off += 2;   /* UDP payload size */
        buf[off++] = 0;                       /* extended rcode */
        buf[off++] = 0;                       /* version */
        buf[off++] = 0x80;                    /* DO bit (high bit) */
        buf[off++] = 0;
        put_u16(buf + off, 0); off += 2;      /* rdlen */
    }

    *out_len = off;
    return 0;
}

/* Skip a DNS name starting at off. Handles compression pointers.
   Returns new offset after the name, or -1 on error. */
static int skip_name(const uint8_t *buf, size_t buflen, size_t off, size_t *new_off) {
    size_t hops = 0;
    while (off < buflen) {
        uint8_t b = buf[off];
        if ((b & 0xc0) == 0xc0) {
            if (off + 2 > buflen) return -1;
            off += 2;
            *new_off = off;
            return 0;
        }
        if (b == 0) {
            off++;
            *new_off = off;
            return 0;
        }
        if (b > 63) return -1;
        off += 1 + b;
        if (++hops > 128) return -1;
    }
    return -1;
}

/* Follow a possibly-compressed name into ascii output (truncated if needed). */
static int read_name(const uint8_t *buf, size_t buflen, size_t off,
                     char *out, size_t outlen, size_t *new_off) {
    size_t op = 0;
    size_t jumps = 0;
    int set_new_off = 0;
    size_t after_name = 0;

    while (off < buflen) {
        uint8_t b = buf[off];
        if ((b & 0xc0) == 0xc0) {
            if (off + 2 > buflen) return -1;
            uint16_t ptr = ((uint16_t)(b & 0x3f) << 8) | buf[off + 1];
            if (!set_new_off) { after_name = off + 2; set_new_off = 1; }
            off = ptr;
            if (++jumps > 16) return -1;
            continue;
        }
        if (b == 0) {
            if (!set_new_off) after_name = off + 1;
            if (out && op < outlen) out[op] = '\0';
            *new_off = after_name;
            return 0;
        }
        if (b > 63) return -1;
        off++;
        if (off + b > buflen) return -1;
        if (op && out && op < outlen) out[op++] = '.';
        for (uint8_t i = 0; i < b; i++) {
            if (out && op < outlen - 1) out[op++] = (char)buf[off + i];
        }
        off += b;
    }
    return -1;
}

int dnsb_pkt_parse_response(const uint8_t *buf, size_t buflen, dnsb_response_info *out) {
    if (buflen < 12) return -1;
    memset(out, 0, sizeof(*out));

    out->id   = get_u16(buf + 0);
    uint16_t flags = get_u16(buf + 2);
    out->rcode = flags & 0x000f;
    out->truncated          = (flags & DNSB_FLAG_TC) ? 1 : 0;
    out->ad                 = (flags & DNSB_FLAG_AD) ? 1 : 0;
    out->recursion_available = (flags & DNSB_FLAG_RA) ? 1 : 0;

    uint16_t qdcount = get_u16(buf + 4);
    uint16_t ancount = get_u16(buf + 6);
    uint16_t nscount = get_u16(buf + 8);
    uint16_t arcount = get_u16(buf + 10);
    out->answers     = ancount;
    out->authorities = nscount;
    out->additional  = arcount;

    size_t off = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        if (skip_name(buf, buflen, off, &off) != 0) return -1;
        if (off + 4 > buflen) return -1;
        off += 4;
    }

    char name[256];
    for (uint16_t i = 0; i < ancount; i++) {
        if (read_name(buf, buflen, off, name, sizeof(name), &off) != 0) return -1;
        if (off + 10 > buflen) return -1;
        uint16_t type = get_u16(buf + off); off += 2;
        off += 2;                                   /* class */
        (void)get_u32(buf + off); off += 4;          /* TTL */
        uint16_t rdlen = get_u16(buf + off); off += 2;
        if (off + rdlen > buflen) return -1;
        if (out->answer_family == 0) {
            if (type == DNSB_TYPE_A && rdlen == 4) {
                out->answer_family = AF_INET;
                memcpy(out->answer_addr, buf + off, 4);
            } else if (type == DNSB_TYPE_AAAA && rdlen == 16) {
                out->answer_family = AF_INET6;
                memcpy(out->answer_addr, buf + off, 16);
            }
        }
        off += rdlen;
    }
    return 0;
}
