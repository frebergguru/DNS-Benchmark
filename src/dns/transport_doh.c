#include "transport.h"

#include "../net/time_ns.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CURL              *easy;
    struct curl_slist *headers;
    char              *url;
    struct curl_slist *connect_to;
} doh_state;

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
} recv_ctx;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    recv_ctx *ctx = userdata;
    size_t n = size * nmemb;
    if (ctx->len + n > ctx->cap) return 0;
    memcpy(ctx->buf + ctx->len, ptr, n);
    ctx->len += n;
    return n;
}

static doh_state *get_state(void **state_inout, const dnsb_endpoint *ep, const char *host) {
    if (*state_inout) return *state_inout;

    doh_state *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->easy = curl_easy_init();
    if (!s->easy) { free(s); return NULL; }

    const char *h = (host && *host) ? host : ep->addr_str;
    /* Bracket IPv6 literals when used in URLs. */
    int is_v6 = (ep->family == AF_INET6 && (!host || !*host));
    s->url = malloc(strlen(h) + 32);
    if (!s->url) { curl_easy_cleanup(s->easy); free(s); return NULL; }
    if (is_v6) sprintf(s->url, "https://[%s]/dns-query", h);
    else       sprintf(s->url, "https://%s/dns-query", h);

    /* CONNECT_TO entry forces the TCP target to the resolver's IP regardless
       of DNS lookups on the system. */
    if (host && *host) {
        char ct[256];
        if (ep->family == AF_INET6)
            snprintf(ct, sizeof(ct), "%s:%d:[%s]:%d", host, ep->port, ep->addr_str, ep->port);
        else
            snprintf(ct, sizeof(ct), "%s:%d:%s:%d", host, ep->port, ep->addr_str, ep->port);
        s->connect_to = curl_slist_append(NULL, ct);
    }

    s->headers = curl_slist_append(NULL, "Content-Type: application/dns-message");
    s->headers = curl_slist_append(s->headers, "Accept: application/dns-message");

    curl_easy_setopt(s->easy, CURLOPT_URL, s->url);
    curl_easy_setopt(s->easy, CURLOPT_POST, 1L);
    curl_easy_setopt(s->easy, CURLOPT_HTTPHEADER, s->headers);
    curl_easy_setopt(s->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(s->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(s->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(s->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(s->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(s->easy, CURLOPT_FOLLOWLOCATION, 0L);
    if (s->connect_to) curl_easy_setopt(s->easy, CURLOPT_CONNECT_TO, s->connect_to);

    *state_inout = s;
    return s;
}

int dnsb_doh_exchange(void **state_inout,
                      const dnsb_endpoint *ep, const char *host,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns) {
    doh_state *s = get_state(state_inout, ep, host);
    if (!s) return -1;

    recv_ctx ctx = { .buf = resp_buf, .cap = resp_cap, .len = 0 };
    curl_easy_setopt(s->easy, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(s->easy, CURLOPT_POSTFIELDSIZE, (long)query_len);
    /* COPYPOSTFIELDS makes curl take an owning copy of the body, so we
       don't leave a dangling pointer to the caller's stack buffer in the
       easy handle between exchanges. */
    curl_easy_setopt(s->easy, CURLOPT_COPYPOSTFIELDS, (const char *)query);
    curl_easy_setopt(s->easy, CURLOPT_TIMEOUT_MS, (long)timeout_ms);

    uint64_t t0 = dnsb_now_ns();
    CURLcode rc = curl_easy_perform(s->easy);
    uint64_t t1 = dnsb_now_ns();

    if (rc == CURLE_OPERATION_TIMEDOUT) return -2;
    if (rc != CURLE_OK) return -1;

    long status = 0;
    curl_easy_getinfo(s->easy, CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) return -1;

    *resp_len = ctx.len;
    *rtt_ns = t1 - t0;
    return 0;
}

void dnsb_doh_free_state(void *state) {
    doh_state *s = state;
    if (!s) return;
    if (s->easy) curl_easy_cleanup(s->easy);
    if (s->headers) curl_slist_free_all(s->headers);
    if (s->connect_to) curl_slist_free_all(s->connect_to);
    free(s->url);
    free(s);
}
