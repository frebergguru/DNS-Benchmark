#include "transport.h"

#include "../net/time_ns.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <glib.h>

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define poll WSAPoll
#else
#include <poll.h>
#endif

typedef struct {
    SSL_CTX     *ctx;
    SSL_SESSION *session;     /* last resumable session */
    char        *sni;          /* hostname for SNI / cert verification */
} dot_state;

static int new_session_cb(SSL *ssl, SSL_SESSION *sess) {
    dot_state *s = SSL_get_ex_data(ssl, 0);
    if (!s) return 0;
    if (s->session) SSL_SESSION_free(s->session);
    s->session = sess;     /* take ownership */
    return 1;
}

static gsize ossl_init_once = 0;

static dot_state *get_state(void **state_inout, const dnsb_endpoint *ep, const char *sni) {
    if (*state_inout) return *state_inout;
    dot_state *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    /* One-time, race-free init across all DoT worker threads. */
    if (g_once_init_enter(&ossl_init_once)) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
        g_once_init_leave(&ossl_init_once, 1);
    }

    s->ctx = SSL_CTX_new(TLS_client_method());
    if (!s->ctx) { free(s); return NULL; }
    SSL_CTX_set_min_proto_version(s->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(s->ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(s->ctx);
    SSL_CTX_set_session_cache_mode(s->ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    SSL_CTX_sess_set_new_cb(s->ctx, new_session_cb);

    s->sni = (sni && *sni) ? strdup(sni) : strdup(ep->addr_str);

    *state_inout = s;
    return s;
}

static int wait_io(dnsb_sock fd, short events, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
    return poll(&pfd, 1, timeout_ms);
}

/* Returns DNSB_INVALID_SOCK on error / timeout. dnsb_sock is wider than int
   on Windows (SOCKET == ULONG_PTR), so we MUST return the native type — the
   previous (int) cast truncated handles whose numeric value exceeded INT_MAX
   and silently broke SSL_set_fd. */
static dnsb_sock connect_socket(const dnsb_endpoint *ep, int timeout_ms, int *err_out) {
    *err_out = -1;
    dnsb_sock fd = socket(ep->family, SOCK_STREAM, 0);
    if (fd == DNSB_INVALID_SOCK) return DNSB_INVALID_SOCK;
    if (dnsb_set_nonblocking(fd) != 0) { dnsb_closesock(fd); return DNSB_INVALID_SOCK; }

    int rc = connect(fd, (const struct sockaddr *)&ep->sa, ep->sa_len);
    if (rc != 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        int inprog = (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS);
#else
        int inprog = (errno == EINPROGRESS);
#endif
        if (!inprog) { dnsb_closesock(fd); return DNSB_INVALID_SOCK; }
        int pr = wait_io(fd, POLLOUT, timeout_ms);
        if (pr <= 0) {
            dnsb_closesock(fd);
            *err_out = (pr == 0) ? -2 : -1;
            return DNSB_INVALID_SOCK;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_err, &slen);
        if (so_err != 0) { dnsb_closesock(fd); return DNSB_INVALID_SOCK; }
    }
    *err_out = 0;
    return fd;
}

/* SSL_set_fd's prototype takes an int even though on Windows the underlying
   socket handle is a SOCKET (ULONG_PTR). OpenSSL casts it back internally,
   so passing the dnsb_sock through (int) cast is wire-correct on Windows
   provided the SOCKET value fits in int. To stay safe we keep dnsb_sock as
   the canonical type elsewhere; only this single call sees the cast. */
static int ssl_set_native_fd(SSL *ssl, dnsb_sock fd) {
#ifdef _WIN32
    return SSL_set_fd(ssl, (int)(intptr_t)fd);
#else
    return SSL_set_fd(ssl, fd);
#endif
}

static int do_handshake(SSL *ssl, dnsb_sock fd, uint64_t deadline_ns) {
    for (;;) {
        int r = SSL_connect(ssl);
        if (r == 1) return 0;
        int err = SSL_get_error(ssl, r);
        uint64_t now = dnsb_now_ns();
        if (now >= deadline_ns) return -2;
        int remain = (int)((deadline_ns - now) / 1000000ULL);
        if (err == SSL_ERROR_WANT_READ)  { if (wait_io(fd, POLLIN, remain)  <= 0) return -2; }
        else if (err == SSL_ERROR_WANT_WRITE) { if (wait_io(fd, POLLOUT, remain) <= 0) return -2; }
        else return -1;
    }
}

static int ssl_send_all(SSL *ssl, dnsb_sock fd, const uint8_t *buf, size_t n, uint64_t deadline_ns) {
    size_t off = 0;
    while (off < n) {
        int r = SSL_write(ssl, buf + off, (int)(n - off));
        if (r > 0) { off += (size_t)r; continue; }
        int err = SSL_get_error(ssl, r);
        uint64_t now = dnsb_now_ns();
        if (now >= deadline_ns) return -2;
        int remain = (int)((deadline_ns - now) / 1000000ULL);
        if (err == SSL_ERROR_WANT_READ)  { if (wait_io(fd, POLLIN, remain)  <= 0) return -2; }
        else if (err == SSL_ERROR_WANT_WRITE) { if (wait_io(fd, POLLOUT, remain) <= 0) return -2; }
        else return -1;
    }
    return 0;
}

static int ssl_recv_n(SSL *ssl, dnsb_sock fd, uint8_t *buf, size_t need, uint64_t deadline_ns) {
    size_t off = 0;
    while (off < need) {
        int r = SSL_read(ssl, buf + off, (int)(need - off));
        if (r > 0) { off += (size_t)r; continue; }
        int err = SSL_get_error(ssl, r);
        if (err == SSL_ERROR_ZERO_RETURN) return -1;
        uint64_t now = dnsb_now_ns();
        if (now >= deadline_ns) return -2;
        int remain = (int)((deadline_ns - now) / 1000000ULL);
        if (err == SSL_ERROR_WANT_READ)  { if (wait_io(fd, POLLIN, remain)  <= 0) return -2; }
        else if (err == SSL_ERROR_WANT_WRITE) { if (wait_io(fd, POLLOUT, remain) <= 0) return -2; }
        else return -1;
    }
    return 0;
}

int dnsb_dot_exchange(void **state_inout,
                      const dnsb_endpoint *ep, const char *sni,
                      const uint8_t *query, size_t query_len,
                      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len,
                      int timeout_ms, uint64_t *rtt_ns) {
    if (query_len > 65535) return -1;
    dot_state *s = get_state(state_inout, ep, sni);
    if (!s) return -1;

    uint64_t t0 = dnsb_now_ns();
    uint64_t deadline = t0 + (uint64_t)timeout_ms * 1000000ULL;

    int cerr = 0;
    dnsb_sock fd = connect_socket(ep, timeout_ms, &cerr);
    if (fd == DNSB_INVALID_SOCK) return cerr;

    SSL *ssl = SSL_new(s->ctx);
    if (!ssl) { dnsb_closesock(fd); return -1; }
    SSL_set_ex_data(ssl, 0, s);

    /* SNI + hostname/IP verification. */
    if (!(ep->family == AF_INET6 && strchr(s->sni, ':'))) {
        SSL_set_tlsext_host_name(ssl, s->sni);
    }
    X509_VERIFY_PARAM *vp = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(vp, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    /* If sni looks like an IP, verify it as an IP, otherwise as a hostname. */
    if (X509_VERIFY_PARAM_set1_ip_asc(vp, s->sni) != 1)
        X509_VERIFY_PARAM_set1_host(vp, s->sni, 0);

    if (s->session) SSL_set_session(ssl, s->session);

    if (ssl_set_native_fd(ssl, fd) != 1) { SSL_free(ssl); dnsb_closesock(fd); return -1; }
    int hr = do_handshake(ssl, fd, deadline);
    if (hr != 0) {
        SSL_free(ssl); dnsb_closesock(fd);
        return hr == -2 ? -2 : -1;
    }

    uint8_t frame[2 + 65536];
    frame[0] = (uint8_t)(query_len >> 8);
    frame[1] = (uint8_t)(query_len & 0xff);
    memcpy(frame + 2, query, query_len);
    /* Same as transport_tcp.c: propagate the -2/-1 distinction from the
       helpers so timeouts after the TLS handshake aren't conflated with
       hard errors. */
    int sr = ssl_send_all(ssl, fd, frame, 2 + query_len, deadline);
    if (sr != 0) {
        SSL_shutdown(ssl); SSL_free(ssl); dnsb_closesock(fd); return sr;
    }

    uint8_t lenbuf[2];
    int rr = ssl_recv_n(ssl, fd, lenbuf, 2, deadline);
    if (rr != 0) {
        SSL_shutdown(ssl); SSL_free(ssl); dnsb_closesock(fd); return rr;
    }
    size_t rlen = ((size_t)lenbuf[0] << 8) | lenbuf[1];
    if (rlen > resp_cap) { SSL_shutdown(ssl); SSL_free(ssl); dnsb_closesock(fd); return -1; }
    rr = ssl_recv_n(ssl, fd, resp_buf, rlen, deadline);
    if (rr != 0) {
        SSL_shutdown(ssl); SSL_free(ssl); dnsb_closesock(fd); return rr;
    }

    *resp_len = rlen;
    *rtt_ns = dnsb_now_ns() - t0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    dnsb_closesock(fd);
    return 0;
}

void dnsb_dot_free_state(void *state) {
    dot_state *s = state;
    if (!s) return;
    if (s->session) SSL_SESSION_free(s->session);
    if (s->ctx) SSL_CTX_free(s->ctx);
    free(s->sni);
    free(s);
}
