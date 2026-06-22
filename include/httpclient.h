/* =========================================================================
   include/httpclient.h — a small, classy HTTP/HTTPS client for ClassyC
   =========================================================================

   Header-only.  Fetch a URL or call a JSON API in one line, then work with
   the result the ClassyC way: status as an int, headers as a `dict`, body as
   a `String`, and `List<String>` for header collections.

   ── Quick start ───────────────────────────────────────────────────────────

       #include "include/httpclient.h"

       auto resp = Http.get("https://pokeapi.co/api/v2/pokemon/ditto");
       defer delete resp;

       if (resp->ok()) {
           dict d = resp->asDict();              // parse JSON body -> dict
           printf("#%d %s\n", (int)d.id, (char*)d.name);
       } else {
           printf("HTTP %d  %s\n", resp->status, (char*)resp->error);
       }

   ── API ───────────────────────────────────────────────────────────────────

       Http.get(url)                         -> HttpResponse*
       Http.get(url, List<String>* headers)  -> HttpResponse*   // extra headers
       Http.post(url, contentType, body)     -> HttpResponse*
       Http.put(url, contentType, body)      -> HttpResponse*
       Http.del(url)                         -> HttpResponse*
       Http.request(method, url, headers, body) -> HttpResponse*

       Http.download(url, FILE* out)         -> HttpResponse*   // stream body
       Http.download(method, url, headers, body, FILE* out) -> HttpResponse*

       Extra headers are passed as a List<String> of "Name: Value" lines.

   ── HttpResponse ───────────────────────────────────────────────────────────

       int     status        // 200, 404, ... (0 on a transport error)
       String  statusText    // "OK", "Not Found", ...
       String  error         // NULL on success, message on transport error
       dict    headers       // response headers (lower-cased names)
       String  body          // decoded response body (chunked is handled)

       resp->ok()                  // 1 when no error and status in 200..299
       resp->header("name")        // case-insensitive lookup -> String or NULL
       resp->headerNames()         // -> List<String>* (caller deletes)
       resp->asDict()              // parse JSON body -> dict
       resp->length()              // body length in bytes
       resp->Print()               // dump status + headers + body

   ── Notes ──────────────────────────────────────────────────────────────────

   * HTTPS is provided through OpenSSL.  By default libssl is loaded lazily at
     runtime via dlopen ("libssl.so.3" / "libssl.so" / "libssl.dylib") and its
     entry points are resolved once; a single SSL_CTX is then created on first
     use and shared by every request.  Plain HTTP needs nothing beyond POSIX
     sockets, so http-only programs have no extra dependency.
   * Build with -DHTTPCLIENT_LINK_SSL to use <openssl/ssl.h> and link
     libssl/libcrypto into the JIT instead of dlopen (see the "TLS backend"
     section below).  The OpenSSL ABI constants used by the dlopen path are
     documented there.
   * Thread-safety: a shared SSL_CTX is safe to use from multiple threads to
     create connections, but the one-time lazy init is not synchronised — in a
     threaded program, make one request from the main thread first to warm it.
   * Keep-alive by default: the client sends `Connection: keep-alive`, frames
     the response by `Content-Length` or `Transfer-Encoding: chunked`, and
     caches the live TCP/TLS connection in a small pool keyed by
     (host, port, https).  The next request to the same origin reuses it,
     skipping DNS, connect and the TLS handshake.  Before reuse a pooled
     socket is probed for liveness; if the peer dropped it (or it has gone
     stale) the connection is discarded and the request transparently
     redialed.  Servers may opt out with `Connection: close`, which is honored.
   * `Http.download(url, out)` streams the body straight to a FILE* instead of
     buffering it, so large transfers need no full-response allocation.  The
     returned HttpResponse still carries status + headers; `length()` reports
     the number of bytes written and `body` is empty.
   * Each HttpResponse owns one heap buffer (the raw response); `statusText`
     and `body` are views into it, freed together by `delete` / `defer delete`.
   * Blocking I/O with a 20s send/recv timeout.  No redirect following (a 3xx
     is returned as-is so you can read `resp->header("location")`).
   ========================================================================= */

#ifndef CLASSYC_HTTPCLIENT_H
#define CLASSYC_HTTPCLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>   /* for gai_strerror */
#include "list.h"

/* ── POSIX sockets + DNS (declared directly, à la include/tcp.h) ─────────── */

extern int    socket(int domain, int type, int protocol);
extern int    connect(int fd, void *addr, int addrlen);
extern long   send(int fd, void *buf, long len, int flags);
extern long   recv(int fd, void *buf, long len, int flags);
extern int    close(int fd);
extern int    setsockopt(int fd, int level, int optname, void *optval, int optlen);
extern int    getaddrinfo(char *node, char *service, void *hints, void *res);
extern void   freeaddrinfo(void *res);

/* glibc-compatible layout (48 bytes on LP64); we only read a few fields. */
struct c2m_addrinfo {
    int            ai_flags;
    int            ai_family;
    int            ai_socktype;
    int            ai_protocol;
    unsigned int   ai_addrlen;
    void          *ai_addr;
    char          *ai_canonname;
    void          *ai_next;
};

struct c2m_timeval { long tv_sec; long tv_usec; };

/* Socket-option constants (documented here instead of pulling <sys/socket.h>). */
#ifndef C2M_SOL_SOCKET
#  define C2M_SOL_SOCKET  1    /* SOL_SOCKET           */
#endif
#ifndef C2M_SO_RCVTIMEO
#  define C2M_SO_RCVTIMEO 20   /* SO_RCVTIMEO  (Linux) */
#endif
#ifndef C2M_SO_SNDTIMEO
#  define C2M_SO_SNDTIMEO 21   /* SO_SNDTIMEO  (Linux) */
#endif

/* poll(2) + fcntl(2): used to probe pooled keep-alive sockets for liveness
   and to consume TLS post-handshake records without blocking. */
extern int poll(void *fds, unsigned long nfds, int timeout);
extern int fcntl(int fd, int cmd, long arg);
struct c2m_pollfd { int fd; short events; short revents; };
#ifndef C2M_POLLIN
#  define C2M_POLLIN      0x001
#endif
#ifndef C2M_MSG_PEEK
#  define C2M_MSG_PEEK    0x02
#endif
#ifndef C2M_MSG_DONTWAIT
#  define C2M_MSG_DONTWAIT 0x40
#endif
#ifndef C2M_F_GETFL
#  define C2M_F_GETFL     3
#endif
#ifndef C2M_F_SETFL
#  define C2M_F_SETFL     4
#endif
#ifndef C2M_O_NONBLOCK
#  define C2M_O_NONBLOCK  0x800   /* O_NONBLOCK (Linux) */
#endif

/* SIG_IGN for SIGPIPE: a reused keep-alive socket the peer just closed must
   not kill the process on write; we recover by redialing instead. */
extern void *signal(int signum, void *handler);
#ifndef C2M_SIGPIPE
#  define C2M_SIGPIPE     13
#endif

/* ════════════════════════════════════════════════════════════════════════
   TLS backend
   ════════════════════════════════════════════════════════════════════════
   Two ways to obtain OpenSSL:

     (default)  HTTPCLIENT_DLOPEN_SSL — dlopen()/dlsym() libssl at runtime, so
                a JIT'd program needs no link flags and no OpenSSL headers.

     -DHTTPCLIENT_LINK_SSL — use the real <openssl/ssl.h> and link
                libssl/libcrypto into the MIR JIT instead (see
                ext/mir/mir-bin-run.c, which loads extra libraries from
                MIR_LIBS=ssl:crypto; the classyc driver also accepts
                -lssl -lcrypto).  The constants below then come from the real
                headers, and the #ifndef guards keep the two paths in step.
   ──────────────────────────────────────────────────────────────────────── */

#if defined(HTTPCLIENT_LINK_SSL)
#  include <openssl/ssl.h>
#else
#  define HTTPCLIENT_DLOPEN_SSL 1
extern void *dlopen(char *file, int mode);
extern void *dlsym(void *handle, char *name);
extern char *dlerror(void);
#  ifndef C2M_RTLD_NOW
#    define C2M_RTLD_NOW 2     /* RTLD_NOW (dlfcn.h) */
#  endif
#endif

/* OpenSSL ABI constants.  These mirror the real OpenSSL headers and are part
   of OpenSSL's stable ABI (1.1.x and 3.x).  The dlopen backend intentionally
   avoids including <openssl/*.h>, so the few values we use are declared here;
   under HTTPCLIENT_LINK_SSL the real headers supply them and these guards
   simply no-op.  SSL_set_tlsext_host_name(ssl, name) is itself a header macro
   that expands to SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME,
   TLSEXT_NAMETYPE_host_name, name). */
#ifndef SSL_CTRL_SET_TLSEXT_HOSTNAME
#  define SSL_CTRL_SET_TLSEXT_HOSTNAME 55   /* openssl/ssl.h  */
#endif
#ifndef TLSEXT_NAMETYPE_host_name
#  define TLSEXT_NAMETYPE_host_name    0    /* openssl/tls1.h */
#endif

/* A single connection — plain (ssl == NULL) or TLS. */
struct c2m_http_conn {
    int   fd;
    void *ssl;   /* an OpenSSL SSL* for https connections, else NULL */
};

#if defined(HTTPCLIENT_DLOPEN_SSL)
/* ── dlopen backend: libssl resolved once, entry points cached ──────────── */
struct c2m_ssl_api {
    int    loaded;
    void  *lib;
    void* (*TLS_client_method)();
    void* (*SSL_CTX_new)(void *method);
    void  (*SSL_CTX_free)(void *ctx);
    void* (*SSL_new)(void *ctx);
    int   (*SSL_set_fd)(void *ssl, int fd);
    long  (*SSL_ctrl)(void *ssl, int cmd, long larg, void *parg);
    int   (*SSL_connect)(void *ssl);
    int   (*SSL_write)(void *ssl, void *buf, int num);
    int   (*SSL_read)(void *ssl, void *buf, int num);
    int   (*SSL_shutdown)(void *ssl);
    void  (*SSL_free)(void *ssl);
};
static struct c2m_ssl_api c2m_ssl;

/* Resolve libssl exactly once (handle + symbols cached for the whole run). */
static int c2m_tls_available(void) {
    if (c2m_ssl.loaded) return c2m_ssl.lib != NULL;
    c2m_ssl.loaded = 1;

    c2m_ssl.lib = dlopen("libssl.so.3", C2M_RTLD_NOW);
    if (c2m_ssl.lib == NULL) c2m_ssl.lib = dlopen("libssl.so.1.1", C2M_RTLD_NOW);
    if (c2m_ssl.lib == NULL) c2m_ssl.lib = dlopen("libssl.so", C2M_RTLD_NOW);
    if (c2m_ssl.lib == NULL) c2m_ssl.lib = dlopen("libssl.dylib", C2M_RTLD_NOW);
    if (c2m_ssl.lib == NULL) {
        fprintf(stderr, "Failed to load libssl: %s\n", dlerror());
        return 0;
    }

    c2m_ssl.TLS_client_method = dlsym(c2m_ssl.lib, "TLS_client_method");
    c2m_ssl.SSL_CTX_new       = dlsym(c2m_ssl.lib, "SSL_CTX_new");
    c2m_ssl.SSL_CTX_free      = dlsym(c2m_ssl.lib, "SSL_CTX_free");
    c2m_ssl.SSL_new           = dlsym(c2m_ssl.lib, "SSL_new");
    c2m_ssl.SSL_set_fd        = dlsym(c2m_ssl.lib, "SSL_set_fd");
    c2m_ssl.SSL_ctrl          = dlsym(c2m_ssl.lib, "SSL_ctrl");
    c2m_ssl.SSL_connect       = dlsym(c2m_ssl.lib, "SSL_connect");
    c2m_ssl.SSL_write         = dlsym(c2m_ssl.lib, "SSL_write");
    c2m_ssl.SSL_read          = dlsym(c2m_ssl.lib, "SSL_read");
    c2m_ssl.SSL_shutdown      = dlsym(c2m_ssl.lib, "SSL_shutdown");
    c2m_ssl.SSL_free          = dlsym(c2m_ssl.lib, "SSL_free");

    if (c2m_ssl.TLS_client_method == NULL || c2m_ssl.SSL_CTX_new == NULL
        || c2m_ssl.SSL_new == NULL || c2m_ssl.SSL_connect == NULL
        || c2m_ssl.SSL_write == NULL || c2m_ssl.SSL_read == NULL) {
        c2m_ssl.lib = NULL;
        return 0;
    }
    return 1;
}

/* Thin, backend-neutral TLS operations (dlopen variant). */
static void *c2m_tls_ctx_new(void)               { return c2m_ssl.SSL_CTX_new(c2m_ssl.TLS_client_method()); }
static void *c2m_tls_new(void *ctx)              { return c2m_ssl.SSL_new(ctx); }
static void  c2m_tls_set_fd(void *s, int fd)     { c2m_ssl.SSL_set_fd(s, fd); }
static void  c2m_tls_set_host(void *s, char *h)  { c2m_ssl.SSL_ctrl(s, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, (void *)h); }
static int   c2m_tls_connect(void *s)            { return c2m_ssl.SSL_connect(s); }
static int   c2m_tls_read(void *s, void *b, int n)  { return c2m_ssl.SSL_read(s, b, n); }
static int   c2m_tls_write(void *s, void *b, int n) { return c2m_ssl.SSL_write(s, b, n); }
static void  c2m_tls_shutdown(void *s)           { c2m_ssl.SSL_shutdown(s); }
static void  c2m_tls_free(void *s)               { c2m_ssl.SSL_free(s); }

#else /* HTTPCLIENT_LINK_SSL: call the real OpenSSL linked into the JIT */

static int   c2m_tls_available(void)             { return 1; }
static void *c2m_tls_ctx_new(void)               { return (void *)SSL_CTX_new(TLS_client_method()); }
static void *c2m_tls_new(void *ctx)              { return (void *)SSL_new((SSL_CTX *)ctx); }
static void  c2m_tls_set_fd(void *s, int fd)     { SSL_set_fd((SSL *)s, fd); }
static void  c2m_tls_set_host(void *s, char *h)  { SSL_set_tlsext_host_name((SSL *)s, h); }
static int   c2m_tls_connect(void *s)            { return SSL_connect((SSL *)s); }
static int   c2m_tls_read(void *s, void *b, int n)  { return SSL_read((SSL *)s, b, n); }
static int   c2m_tls_write(void *s, void *b, int n) { return SSL_write((SSL *)s, b, n); }
static void  c2m_tls_shutdown(void *s)           { SSL_shutdown((SSL *)s); }
static void  c2m_tls_free(void *s)               { SSL_free((SSL *)s); }

#endif /* TLS backend */

/* One SSL_CTX is created on first use and shared by every request — it is NOT
   rebuilt per request.  In OpenSSL 1.1+/3 a single SSL_CTX is safe to share
   across threads to create SSL objects (internal locking, atomic refcounts);
   only this one-time lazy creation is unsynchronised, so a multithreaded
   program should warm it up with one request from the main thread first.  The
   context lives for the life of the process and is reclaimed at exit. */
static void *c2m_http_shared_ctx     = NULL;
static int   c2m_http_shared_ctx_set = 0;

static void *c2m_http_ctx(void) {
    if (c2m_http_shared_ctx_set) return c2m_http_shared_ctx;
    c2m_http_shared_ctx_set = 1;
    if (!c2m_tls_available()) return NULL;
    c2m_http_shared_ctx = c2m_tls_ctx_new();
    return c2m_http_shared_ctx;
}

/* ── Low-level transport (the only "plain C11" plumbing in this header) ───── */

/* Resolve host:port and connect; returns a connected fd or -1. */
/* Resolve host:port and connect; returns a connected fd or -1. */
static int c2m_http_dial(char *host, char *port) {
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    struct addrinfo *ai = NULL;
    int fd = -1;
    int gai_err;
    char addrstr[128];

#ifdef DEBUG
    fprintf(stderr, "[c2m] c2m_http_dial: resolving %s:%s\n", host, port);
#endif

    /* Only ask for TCP streams (avoids the useless UDP entry you saw) */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_ADDRCONFIG;   /* optional but recommended */

    gai_err = getaddrinfo(host, port, &hints, &res);
    if (gai_err != 0 || res == NULL) {
        fprintf(stderr, "[c2m] c2m_http_dial: getaddrinfo(%s:%s) FAILED: %s (gai_err=%d)\n",
                host, port, gai_strerror(gai_err), gai_err);
        return -1;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next) {
        /* Convert address to string for logging */
        addrstr[0] = '\0';
        if (ai->ai_addr != NULL) {
            if (ai->ai_family == AF_INET) {
                inet_ntop(AF_INET,
                          &((struct sockaddr_in *)ai->ai_addr)->sin_addr,
                          addrstr, sizeof(addrstr));
            } else if (ai->ai_family == AF_INET6) {
                inet_ntop(AF_INET6,
                          &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr,
                          addrstr, sizeof(addrstr));
            } else {
                snprintf(addrstr, sizeof(addrstr), "family=%d", ai->ai_family);
            }
        }
#if DEBUG
        fprintf(stderr,
                "[c2m] c2m_http_dial: trying %s:%s (family=%d socktype=%d proto=%d addr=%s)\n",
                host, port, ai->ai_family, ai->ai_socktype, ai->ai_protocol,
                addrstr[0] ? addrstr : "(no addr)");
#endif

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            int err = errno;
            fprintf(stderr, "[c2m] c2m_http_dial: socket() FAILED for %s: %s (errno=%d)\n",
                    addrstr, strerror(err), err);
            continue;
        }

        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
#if DEBUG
            fprintf(stderr, "[c2m] c2m_http_dial: CONNECTED to %s:%s (%s) fd=%d\n",
                    host, port, addrstr, fd);
#endif
            break;
        }

#if DEBUG
        int err = errno;
        fprintf(stderr, "[c2m] c2m_http_dial: connect() to %s:%s (%s) FAILED: %s (errno=%d)\n",
                host, port, addrstr, strerror(err), err);
#endif

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd >= 0) {
        struct c2m_timeval tv;
        tv.tv_sec = 20;
        tv.tv_usec = 0;
        setsockopt(fd, C2M_SOL_SOCKET, C2M_SO_RCVTIMEO, (void *)&tv, sizeof(tv));
        setsockopt(fd, C2M_SOL_SOCKET, C2M_SO_SNDTIMEO, (void *)&tv, sizeof(tv));
        //fprintf(stderr, "[c2m] c2m_http_dial: set 20s RCV/SND timeouts on fd=%d\n", fd);
    } else {
        fprintf(stderr, "[c2m] c2m_http_dial: ALL connection attempts to %s:%s FAILED\n",
                host, port);
    }

    return fd;
}

/* Perform the TLS handshake on an already-connected socket, reusing the one
   shared SSL_CTX.  Returns 0 on success. */
static int c2m_http_tls_handshake(struct c2m_http_conn *c, char *host) {
    void *ctx = c2m_http_ctx();
    if (ctx == NULL) return -1;

    c->ssl = c2m_tls_new(ctx);
    if (c->ssl == NULL) return -1;

    c2m_tls_set_fd(c->ssl, c->fd);
    c2m_tls_set_host(c->ssl, host);   /* SNI */

    if (c2m_tls_connect(c->ssl) != 1) return -1;
    return 0;
}

static long c2m_http_conn_read(struct c2m_http_conn *c, char *buf, long len) {
    if (c->ssl != NULL) return (long)c2m_tls_read(c->ssl, (void *)buf, (int)len);
    return recv(c->fd, (void *)buf, len, 0);
}

static long c2m_http_conn_write(struct c2m_http_conn *c, char *buf, long len) {
    if (c->ssl != NULL) return (long)c2m_tls_write(c->ssl, (void *)buf, (int)len);
    return send(c->fd, (void *)buf, len, 0);
}

static int c2m_http_conn_write_all(struct c2m_http_conn *c, char *buf, long len) {
    long total = 0;
    while (total < len) {
        long n = c2m_http_conn_write(c, buf + total, len - total);
        if (n <= 0) return -1;
        total = total + n;
    }
    return (int)total;
}

static void c2m_http_conn_close(struct c2m_http_conn *c) {
    if (c->ssl != NULL) {
        c2m_tls_shutdown(c->ssl);   /* the shared SSL_CTX is NOT freed here */
        c2m_tls_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

/* Case-insensitive substring test (for "Transfer-Encoding: chunked"). */
static int c2m_http__ci_contains(char *hay, char *needle) {
    if (hay == NULL || needle == NULL) return 0;
    for (int i = 0; hay[i] != 0; i++) {
        int j = 0;
        while (needle[j] != 0 && hay[i + j] != 0) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = a + 32;
            if (b >= 'A' && b <= 'Z') b = b + 32;
            if (a != b) break;
            j++;
        }
        if (needle[j] == 0) return 1;
    }
    return 0;
}

/* Decode a chunked transfer-encoding body in place. */
static void c2m_http_dechunk(char *body, long len, long *out_len) {
    char *src = body;
    char *end = body + len;
    char *dst = body;

    while (src < end) {
        long sz  = 0;
        int  any = 0;
        while (src < end) {
            char ch = *src;
            int  d  = -1;
            if (ch >= '0' && ch <= '9')      d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            if (d < 0) break;
            sz  = sz * 16 + d;
            any = 1;
            src++;
        }
        /* skip rest of the size line (any chunk extensions, then CRLF) */
        while (src < end && *src != '\n') src++;
        if (src < end) src++;

        if (!any) break;
        if (sz == 0) break;                 /* final chunk */
        if (src + sz > end) sz = end - src; /* clamp on truncation */

        memmove(dst, src, sz);
        dst = dst + sz;
        src = src + sz;

        if (src < end && *src == '\r') src++;
        if (src < end && *src == '\n') src++;
    }
    *dst = 0;
    *out_len = dst - body;
}

/* ══════════════════════════════════════════════════════════════════════════
   Framed reads + keep-alive connection pool
   ══════════════════════════════════════════════════════════════════════════
   To reuse a socket across requests we can no longer "read to EOF": the
   connection stays open, so each response must be delimited exactly.  The
   helpers below parse just enough of the response head to honour
   Content-Length / Transfer-Encoding: chunked framing, then a small pool keeps
   idle connections keyed by (host, port, secure).  Reused connections are
   probed for liveness first and, if the peer dropped them, transparently
   redialed.
   ══════════════════════════════════════════════════════════════════════════ */

/* Grow `buf` (capacity *cap) to hold at least `need` bytes; returns the
   (possibly moved) buffer, or NULL on allocation failure. */
static char *c2m_http_grow(char *buf, long *cap, long need) {
    if (need <= *cap) return buf;
    long c = *cap;
    while (c < need) c = c * 2;
    char *nb = (char *)realloc(buf, c);
    if (nb == NULL) return NULL;
    *cap = c;
    return nb;
}

/* Find header `name` (case-insensitive) within the header block [start,end).
   Returns a pointer to its value (leading whitespace skipped) and the value
   length via *out_len, or NULL when the header is absent. */
static char *c2m_http_hdr(char *start, char *end, char *name, int *out_len) {
    int nlen = (int)strlen(name);
    char *p = start;
    *out_len = 0;
    while (p < end) {
        /* NB: written as an explicit `break` rather than
           `while (eol + 1 < end && !(eol[0]=='\r' && eol[1]=='\n'))` because the
           MIR JIT (c2mir) miscompiles that `&& !( … && … )` while-condition. */
        char *eol = p;
        while (eol + 1 < end) {
            if (eol[0] == '\r' && eol[1] == '\n') break;
            eol++;
        }
        if (eol + 1 >= end) eol = end;
        if (eol - p > nlen && p[nlen] == ':') {
            int match = 1;
            for (int i = 0; i < nlen; i++) {
                char a = p[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = a + 32;
                if (b >= 'A' && b <= 'Z') b = b + 32;
                if (a != b) { match = 0; break; }
            }
            if (match) {
                char *v = p + nlen + 1;
                while (v < eol && (*v == ' ' || *v == '\t')) v++;
                *out_len = (int)(eol - v);
                return v;
            }
        }
        if (eol >= end) break;
        p = eol + 2;
    }
    return NULL;
}

/* Case-insensitive token search within a length-bounded string. */
static int c2m_http__ci_has(char *s, int slen, char *needle) {
    int nlen = (int)strlen(needle);
    if (nlen == 0 || slen < nlen) return 0;
    for (int i = 0; i + nlen <= slen; i++) {
        int j = 0;
        for (; j < nlen; j++) {
            char a = s[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = a + 32;
            if (b >= 'A' && b <= 'Z') b = b + 32;
            if (a != b) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

/* Has the chunked body in [body,body+n) reached its terminating 0-size chunk?
   Returns 1 when the full chunked message is present, 0 when more is needed.
   Trailers are not parsed; any stray bytes left on the socket are caught by
   the pool's liveness probe and cause the connection to be discarded. */
static int c2m_http_chunked_complete(char *body, long n) {
    char *p = body;
    char *end = body + n;
    while (p < end) {
        long sz = 0;
        int  any = 0;
        while (p < end) {
            char ch = *p; int d = -1;
            if (ch >= '0' && ch <= '9')      d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            if (d < 0) break;
            sz = sz * 16 + d; any = 1; p++;
        }
        char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        if (nl >= end) return 0;            /* size line not fully read yet */
        p = nl + 1;
        if (!any) return 1;                 /* malformed; stop reading */
        if (sz == 0) return (end - p >= 2); /* terminator reached */
        if (end - p < sz + 2) return 0;     /* data + CRLF not fully read */
        p = p + sz;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    return 0;
}

/* Inspect the header block (buf .. buf+body_off) and report the body framing:
   *chunked, *content_len (-1 if absent) and *reusable (1 when the connection
   may be kept alive afterwards). */
static void c2m_http_framing(char *buf, long body_off,
                             int *chunked, long *content_len, int *reusable) {
    char *hbeg = buf;
    char *hend = buf + body_off - 4;             /* the "\r\n\r\n" */
    int   http11 = (strncmp(buf, "HTTP/1.0", 8) != 0);

    int tlen = 0, clen = 0, cnlen = 0;
    char *te = c2m_http_hdr(hbeg, hend, "transfer-encoding", &tlen);
    char *cl = c2m_http_hdr(hbeg, hend, "content-length",    &clen);
    char *cn = c2m_http_hdr(hbeg, hend, "connection",        &cnlen);

    *chunked = (te != NULL && c2m_http__ci_has(te, tlen, "chunked"));
    *content_len = -1;
    if (!*chunked && cl != NULL) {
        long v = 0; int got = 0;
        for (int i = 0; i < clen; i++) {
            char ch = cl[i];
            if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); got = 1; }
            else break;
        }
        if (got) *content_len = v;
    }
    int conn_close = (cn != NULL && c2m_http__ci_has(cn, cnlen, "close"));
    int conn_keep  = (cn != NULL && c2m_http__ci_has(cn, cnlen, "keep-alive"));
    *reusable = http11 ? !conn_close : conn_keep;
}

/* Read one complete response into a NUL-terminated malloc'd buffer, honouring
   Content-Length / chunked framing so the socket can be reused.  Unframed
   responses fall back to read-to-EOF (and cannot be kept alive).  *out_keep is
   set to 1 when the connection may be returned to the pool. */
static char *c2m_http_read_message(struct c2m_http_conn *c, long *out_len, int *out_keep) {
    long  cap = 16384, len = 0;
    char *buf = (char *)malloc(cap);
    *out_len = 0; *out_keep = 0;
    if (buf == NULL) return NULL;

    /* phase 1: read until the end of the header block */
    long body_off = -1;
    while (1) {
        buf = c2m_http_grow(buf, &cap, len + 8192 + 1);
        if (buf == NULL) return NULL;
        long n = c2m_http_conn_read(c, buf + len, 8192);
        if (n <= 0) break;
        len = len + n; buf[len] = 0;
        char *hb = strstr(buf, "\r\n\r\n");
        if (hb != NULL) { body_off = (hb - buf) + 4; break; }
    }
    if (body_off < 0) { buf[len] = 0; *out_len = len; return buf; }

    int  chunked = 0, reusable = 0;
    long content_len = -1;
    c2m_http_framing(buf, body_off, &chunked, &content_len, &reusable);

    /* phase 2: read the body according to its framing */
    if (chunked) {
        while (!c2m_http_chunked_complete(buf + body_off, len - body_off)) {
            buf = c2m_http_grow(buf, &cap, len + 8192 + 1);
            if (buf == NULL) return NULL;
            long n = c2m_http_conn_read(c, buf + len, 8192);
            if (n <= 0) { reusable = 0; break; }
            len = len + n; buf[len] = 0;
        }
        *out_keep = reusable;
    } else if (content_len >= 0) {
        long need = body_off + content_len;
        buf = c2m_http_grow(buf, &cap, need + 1);
        if (buf == NULL) return NULL;
        while (len < need) {
            long n = c2m_http_conn_read(c, buf + len, need - len);
            if (n <= 0) break;
            len = len + n;
        }
        buf[len] = 0;
        *out_keep = (len >= need) ? reusable : 0;
    } else {
        while (1) {
            buf = c2m_http_grow(buf, &cap, len + 8192 + 1);
            if (buf == NULL) return NULL;
            long n = c2m_http_conn_read(c, buf + len, 8192);
            if (n <= 0) break;
            len = len + n; buf[len] = 0;
        }
        *out_keep = 0;
    }
    *out_len = len;
    return buf;
}

/* Like c2m_http_read_message, but the (decoded) body is streamed to `out`
   instead of being buffered — for large downloads.  Returns a malloc'd buffer
   holding only the header block (NUL-terminated) for HttpResponse::ingest, and
   reports the number of body bytes written via *out_body_len. */
static char *c2m_http_read_message_to(struct c2m_http_conn *c, FILE *out,
                                      long *out_hdr_len, long *out_body_len,
                                      int *out_keep) {
    long  cap = 16384, len = 0;
    char *buf = (char *)malloc(cap);
    *out_hdr_len = 0; *out_body_len = 0; *out_keep = 0;
    if (buf == NULL) return NULL;

    long body_off = -1;
    while (1) {
        buf = c2m_http_grow(buf, &cap, len + 8192 + 1);
        if (buf == NULL) return NULL;
        long n = c2m_http_conn_read(c, buf + len, 8192);
        if (n <= 0) break;
        len = len + n; buf[len] = 0;
        char *hb = strstr(buf, "\r\n\r\n");
        if (hb != NULL) { body_off = (hb - buf) + 4; break; }
    }
    if (body_off < 0) { buf[len] = 0; *out_hdr_len = len; return buf; }

    int  chunked = 0, reusable = 0;
    long content_len = -1;
    c2m_http_framing(buf, body_off, &chunked, &content_len, &reusable);

    long body_have = len - body_off;   /* body bytes already pulled in */
    long streamed  = 0;

    if (chunked) {
        /* Chunked responses are buffered to completion, decoded, then flushed
           (they are usually dynamic and modest in size). */
        while (!c2m_http_chunked_complete(buf + body_off, len - body_off)) {
            buf = c2m_http_grow(buf, &cap, len + 8192 + 1);
            if (buf == NULL) return NULL;
            long n = c2m_http_conn_read(c, buf + len, 8192);
            if (n <= 0) { reusable = 0; break; }
            len = len + n; buf[len] = 0;
        }
        long dec = 0;
        c2m_http_dechunk(buf + body_off, len - body_off, &dec);
        if (dec > 0 && out != NULL) fwrite(buf + body_off, 1, (size_t)dec, out);
        streamed = dec;
        *out_keep = reusable;
    } else if (content_len >= 0) {
        if (body_have > content_len) body_have = content_len;
        if (body_have > 0 && out != NULL) fwrite(buf + body_off, 1, (size_t)body_have, out);
        streamed = body_have;
        char io[8192];
        while (streamed < content_len) {
            long want = content_len - streamed;
            if (want > (long)sizeof(io)) want = (long)sizeof(io);
            long n = c2m_http_conn_read(c, io, want);
            if (n <= 0) break;
            if (out != NULL) fwrite(io, 1, (size_t)n, out);
            streamed = streamed + n;
        }
        *out_keep = (streamed >= content_len) ? reusable : 0;
    } else {
        if (body_have > 0 && out != NULL) fwrite(buf + body_off, 1, (size_t)body_have, out);
        streamed = body_have;
        char io[8192];
        while (1) {
            long n = c2m_http_conn_read(c, io, (long)sizeof(io));
            if (n <= 0) break;
            if (out != NULL) fwrite(io, 1, (size_t)n, out);
            streamed = streamed + n;
        }
        *out_keep = 0;
    }

    /* Trim to just the header block (status + headers + CRLFCRLF) for ingest. */
    buf[body_off] = 0;
    *out_hdr_len  = body_off;
    *out_body_len = streamed;
    return buf;
}

/* ── keep-alive connection pool ─────────────────────────────────────────── */
#ifndef C2M_HTTP_POOL_MAX
#  define C2M_HTTP_POOL_MAX 8
#endif
struct c2m_http_pool_entry {
    int  used;
    char host[256];
    int  port;
    int  secure;
    struct c2m_http_conn conn;
};
static struct c2m_http_pool_entry c2m_http_pool[C2M_HTTP_POOL_MAX];

static int c2m_http_sig_ready = 0;
static void c2m_http_init_once(void) {
    if (c2m_http_sig_ready) return;
    c2m_http_sig_ready = 1;
    signal(C2M_SIGPIPE, (void *)1);   /* SIG_IGN */
}

/* Is a pooled connection still usable?  An idle keep-alive socket should have
   nothing pending; readable bytes mean the peer closed it or left data. */
static int c2m_http_conn_alive(struct c2m_http_conn *c) {
    struct c2m_pollfd pfd;
    pfd.fd = c->fd; pfd.events = C2M_POLLIN; pfd.revents = 0;
    int r = poll(&pfd, 1, 0);
    if (r == 0) return 1;            /* idle -> good */
    if (r <  0) return 0;

    if (c->ssl == NULL) {
        char b;
        long n = recv(c->fd, (void *)&b, 1, C2M_MSG_PEEK | C2M_MSG_DONTWAIT);
        if (n < 0) return 1;         /* spurious wakeup */
        return 0;                     /* EOF or leftover data -> stale */
    }

    /* TLS: readable bytes are often just TLS 1.3 session tickets, not a close.
       Drain them non-blocking; only real app data or a close_notify is fatal. */
    int fl = fcntl(c->fd, C2M_F_GETFL, 0);
    if (fl >= 0) fcntl(c->fd, C2M_F_SETFL, (long)(fl | C2M_O_NONBLOCK));
    char scratch[512];
    long n = (long)c2m_tls_read(c->ssl, scratch, (int)sizeof(scratch));
    if (fl >= 0) fcntl(c->fd, C2M_F_SETFL, (long)fl);
    if (n > 0)  return 0;            /* unexpected application data -> stale */
    if (n == 0) return 0;            /* close_notify -> stale */
    return 1;                         /* WANT_READ after consuming records -> ok */
}

static int c2m_http_pool_take(char *host, int port, int secure, struct c2m_http_conn *out) {
    for (int i = 0; i < C2M_HTTP_POOL_MAX; i++) {
        if (!c2m_http_pool[i].used) continue;
        if (c2m_http_pool[i].port != port || c2m_http_pool[i].secure != secure) continue;
        if (strcmp(c2m_http_pool[i].host, host) != 0) continue;
        struct c2m_http_conn c = c2m_http_pool[i].conn;
        c2m_http_pool[i].used = 0;
        if (!c2m_http_conn_alive(&c)) { c2m_http_conn_close(&c); continue; }
        *out = c;
        return 1;
    }
    return 0;
}

static void c2m_http_pool_put(char *host, int port, int secure, struct c2m_http_conn *c) {
    if (strlen(host) >= 256) { c2m_http_conn_close(c); return; }
    for (int i = 0; i < C2M_HTTP_POOL_MAX; i++) {
        if (c2m_http_pool[i].used) continue;
        c2m_http_pool[i].used   = 1;
        strncpy(c2m_http_pool[i].host, host, 255);
        c2m_http_pool[i].host[255] = 0;
        c2m_http_pool[i].port   = port;
        c2m_http_pool[i].secure = secure;
        c2m_http_pool[i].conn   = *c;
        return;
    }
    c2m_http_conn_close(c);          /* pool full -> just close it */
}

/* Acquire a connection to host:port, reusing a pooled one when possible.
   Returns 0 on success (sets *from_pool), -1 on failure (sets *err). */
static int c2m_http_acquire(char *host, char *portstr, int port, int secure,
                            struct c2m_http_conn *conn, int *from_pool, char **err) {
    conn->fd = -1; conn->ssl = NULL;
    *from_pool = 0;

    if (c2m_http_pool_take(host, port, secure, conn)) { *from_pool = 1; return 0; }

    conn->fd = c2m_http_dial(host, portstr);
    if (conn->fd < 0) { *err = "could not resolve or connect to host"; return -1; }
    if (secure) {
        if (c2m_http_tls_handshake(conn, host) != 0) {
            c2m_http_conn_close(conn);
            *err = "TLS handshake failed (OpenSSL/libssl required for https)";
            return -1;
        }
    }
    return 0;
}

/* Send `req` and read the whole response into a malloc'd buffer.  A pooled
   connection that turns out to be stale (write/first read fails) is silently
   discarded and the request retried once on a fresh socket.  On success the
   connection is returned to the pool when the response allows keep-alive. */
static char *c2m_http_exchange(char *host, int port, int secure,
                               char *req, long reqlen,
                               long *out_len, char **out_err) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    *out_len = 0; *out_err = NULL;
    c2m_http_init_once();

    for (int attempt = 0; attempt < 2; attempt++) {
        struct c2m_http_conn conn;
        int   from_pool = 0;
        char *err = NULL;
        if (c2m_http_acquire(host, portstr, port, secure, &conn, &from_pool, &err) != 0) {
            *out_err = err; return NULL;
        }

        if (c2m_http_conn_write_all(&conn, req, reqlen) < 0) {
            c2m_http_conn_close(&conn);
            if (from_pool) continue;        /* pooled socket went stale: redial */
            *out_err = "failed to send request"; return NULL;
        }

        int  keep = 0;
        long rawlen = 0;
        char *raw = c2m_http_read_message(&conn, &rawlen, &keep);
        if (raw == NULL || rawlen == 0) {
            if (raw != NULL) free(raw);
            c2m_http_conn_close(&conn);
            if (from_pool) continue;        /* pooled socket went stale: redial */
            *out_err = "failed to read response"; return NULL;
        }

        if (keep) c2m_http_pool_put(host, port, secure, &conn);
        else      c2m_http_conn_close(&conn);

        *out_len = rawlen;
        return raw;
    }
    *out_err = "failed to read response";
    return NULL;
}

/* Streaming variant of c2m_http_exchange: the body is written to `out` and the
   returned buffer holds only the response head (for ingest). */
static char *c2m_http_exchange_to(char *host, int port, int secure,
                                  char *req, long reqlen, FILE *out,
                                  long *out_hdr_len, long *out_body_len,
                                  char **out_err) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    *out_hdr_len = 0; *out_body_len = 0; *out_err = NULL;
    c2m_http_init_once();

    for (int attempt = 0; attempt < 2; attempt++) {
        struct c2m_http_conn conn;
        int   from_pool = 0;
        char *err = NULL;
        if (c2m_http_acquire(host, portstr, port, secure, &conn, &from_pool, &err) != 0) {
            *out_err = err; return NULL;
        }

        if (c2m_http_conn_write_all(&conn, req, reqlen) < 0) {
            c2m_http_conn_close(&conn);
            if (from_pool) continue;
            *out_err = "failed to send request"; return NULL;
        }

        int  keep = 0;
        long hlen = 0, blen = 0;
        char *hdr = c2m_http_read_message_to(&conn, out, &hlen, &blen, &keep);
        if (hdr == NULL || hlen == 0) {
            if (hdr != NULL) free(hdr);
            c2m_http_conn_close(&conn);
            if (from_pool) continue;
            *out_err = "failed to read response"; return NULL;
        }

        if (keep) c2m_http_pool_put(host, port, secure, &conn);
        else      c2m_http_conn_close(&conn);

        *out_hdr_len  = hlen;
        *out_body_len = blen;
        return hdr;
    }
    *out_err = "failed to read response";
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
   Url — split "scheme://host[:port][/path]" with String methods
   ══════════════════════════════════════════════════════════════════════════ */
class Url {
    String scheme;
    String host;
    String path;
    int    port;
    int    secure;

    Url(char *url) {
        String s   = url;
        this->scheme = "http";
        this->host   = "";
        this->path   = "/";
        this->port   = 80;
        this->secure = 0;

        /* Strings stored in this object must outlive the constructor's scope,
           so each one is .detach()'d from the automatic String arena. */
        String rest;
        int sep = (int)s.find("://");
        if (sep >= 0) {
            this->scheme = s.substr(0, sep).lower().detach();
            rest = s.substr(sep + 3, (int)s.length() - sep - 3);
        } else {
            rest = s;
        }
        if (strcmp((char *)this->scheme, "https") == 0) {
            this->secure = 1;
            this->port   = 443;
        }

        /* split host[:port] from the path */
        String hostport;
        int slash = (int)rest.find("/");
        if (slash >= 0) {
            hostport   = rest.substr(0, slash);
            this->path = rest.substr(slash, (int)rest.length() - slash).detach();
        } else {
            hostport   = rest;
            this->path = "/";
        }

        /* optional :port */
        int colon = (int)hostport.find(":");
        if (colon >= 0) {
            this->host = hostport.substr(0, colon).detach();
            String ps  = hostport.substr(colon + 1, (int)hostport.length() - colon - 1);
            this->port = atoi((char *)ps);
        } else {
            this->host = hostport.detach();
        }
    }
    ~Url() {}
};

/* ══════════════════════════════════════════════════════════════════════════
   HttpResponse — status + headers (dict) + body (String)
   ══════════════════════════════════════════════════════════════════════════ */
class HttpResponse {
    int    status;       /* status code, or 0 on a transport error           */
    String statusText;   /* reason phrase ("OK", "Not Found", ...)           */
    String error;        /* NULL on success, else a transport error message  */
    dict   headers;      /* response headers, names lower-cased              */
    String body;         /* decoded body (view into `raw`)                   */
    char  *raw;          /* owned heap buffer holding the whole response     */
    long   downloaded;   /* bytes streamed to a FILE* by Http.download (else 0) */
    dict   jsonCache;    /* lazily-parsed body dict (owned); valid iff jsonParsed */
    int    jsonParsed;   /* 1 once asDict() has populated jsonCache            */

    HttpResponse() {
        this->status     = 0;
        this->statusText = "";
        this->error      = NULL;
        this->headers    = {};
        this->body       = "";
        this->raw        = NULL;
        this->downloaded = 0;
        this->jsonParsed = 0;
    }

    ~HttpResponse() {
        /* Free everything this response owns.  The headers dict and the parsed
           JSON dict each hold their own strdup'd copies, so destroying them is
           independent of freeing `raw`.  Without this, every request leaked a
           headers dict (and asDict() leaked a whole JSON tree) — a steady RAM
           climb in any fetch loop. */
        if (this->jsonParsed) delete this->jsonCache;
        delete this->headers;
        if (this->raw != NULL) free((void *)this->raw);
    }

    /* Case-insensitive header lookup; returns the value String or NULL. */
    String header(char *name) {
        char low[256];
        int  i = 0;
        while (name[i] != 0 && i < 255) {
            char ch = name[i];
            if (ch >= 'A' && ch <= 'Z') ch = ch + 32;
            low[i] = ch;
            i++;
        }
        low[i] = 0;
        if ((char *)low in this->headers) return (char *)this->headers[(char *)low];
        return NULL;
    }

    /* Parse a complete, malloc'd response buffer.  Takes ownership of `buf`. */
    void ingest(char *buf, long len) {
        this->raw = buf;
        if (buf == NULL || len <= 0) { this->error = "empty response"; return; }

        char *line_end = strstr(buf, "\r\n");
        if (line_end == NULL) { this->error = "malformed response"; return; }

        /* status line: HTTP/x.y CODE REASON */
        char *sp1 = strchr(buf, ' ');
        if (sp1 != NULL && sp1 < line_end) {
            this->status = atoi(sp1 + 1);
            char *sp2 = strchr(sp1 + 1, ' ');
            *line_end = 0;                          /* end the reason phrase */
            if (sp2 != NULL && sp2 < line_end) this->statusText = sp2 + 1;
        } else {
            *line_end = 0;
        }

        /* header / body boundary */
        char *hdr_start = line_end + 2;
        char *hdr_end   = strstr(hdr_start, "\r\n\r\n");
        char *body_start;
        long  body_len;
        if (hdr_end == NULL) {
            body_start = buf + len;
            body_len   = 0;
            hdr_end    = buf + len;
        } else {
            body_start = hdr_end + 4;
            body_len   = len - (body_start - buf);
            *hdr_end   = 0;
        }

        /* headers -> dict (lower-cased names) */
        char *hp = hdr_start;
        while (hp < hdr_end && *hp != 0) {
            char *eol = strstr(hp, "\r\n");
            if (eol != NULL) *eol = 0;
            char *colon = strchr(hp, ':');
            if (colon != NULL) {
                *colon = 0;
                char *name  = hp;
                char *value = colon + 1;
                while (*value == ' ' || *value == '\t') value++;
                for (int i = 0; name[i] != 0; i++)
                    if (name[i] >= 'A' && name[i] <= 'Z') name[i] = name[i] + 32;
                this->headers[name] = value;
            }
            if (eol == NULL) break;
            hp = eol + 2;
        }

        /* body: decode chunked encoding if present */
        String te = this->header("transfer-encoding");
        if (te != NULL && c2m_http__ci_contains((char *)te, "chunked")) {
            long out = 0;
            c2m_http_dechunk(body_start, body_len, &out);
            body_len = out;
        }
        body_start[body_len] = 0;
        this->body = body_start;
    }

    /* 1 when the request completed and the status is in the 2xx range. */
    int ok() {
        return this->error == NULL && this->status >= 200 && this->status < 300;
    }

    /* All header names as a List<String> (caller deletes the list). */
    List<String>* headerNames() {
        List<String>* names = new List<String>();
        for (auto k in this->headers) names->Add((String)k);
        return names;
    }

    /* Parse the (JSON) body into a dict.  The result is owned by this response
       and reused on subsequent calls; it is freed by `delete` / `defer delete`
       along with the response, so callers must not delete it themselves and
       must not use it after the response is gone. */
    dict asDict() {
        if (!this->jsonParsed) {
            this->jsonCache  = json((char *)this->body);
            this->jsonParsed = 1;
        }
        return this->jsonCache;
    }

    /* Body length in bytes.  For a streamed download (Http.download) the body
       lives in the destination FILE*, so we report the bytes written there. */
    int length() {
        if (this->downloaded > 0) return (int)this->downloaded;
        return (int)strlen((char *)this->body);
    }

    /* Human-readable dump: status line, headers, then body. */
    void Print() {
        if (this->error != NULL) {
            printf("HTTP request failed: %s\n", (char *)this->error);
            return;
        }
        printf("HTTP %d %s\n", this->status, (char *)this->statusText);
        for (auto k in this->headers)
            printf("  %s: %s\n", k, (char *)this->headers[k]);
        printf("\n%s\n", (char *)this->body);
    }
};

/* ══════════════════════════════════════════════════════════════════════════
   Http — static entry points (use like File.open / File.read_text)
   ══════════════════════════════════════════════════════════════════════════ */
class Http {
    /* Assemble the wire request (line + headers + optional body) for a URL. */
    static String buildRequest(char *method, Url *u, List<String>* headers, char *body) {
        String req = method;
        req = req + " " + u->path + " HTTP/1.1\r\n";
        req = req + "Host: " + u->host + "\r\n";
        req = req + "User-Agent: classyc-httpclient/0.1\r\n";
        req = req + "Accept: */*\r\n";
        if (headers != NULL)
            for (auto h in headers)
                req = req + h + "\r\n";
        if (body != NULL) {
            int blen = (int)strlen(body);
            req = req + "Content-Length: " + blen + "\r\n";
        }
        /* Ask to keep the socket open; the transport pools it for reuse and
           honours the server's reply (a `Connection: close` is respected). */
        req = req + "Connection: keep-alive\r\n\r\n";
        if (body != NULL) req = req + body;
        return req;
    }

    /* Full request.  `headers` is an optional List<String> of "Name: Value"
       lines; `body` is an optional request body.  Always returns a non-NULL
       HttpResponse* (check ->ok() / ->error).

       The underlying TCP/TLS connection is cached per (host, port, https) and
       reused on the next request to the same origin, so back-to-back fetches
       skip DNS, connect and the TLS handshake.  A cached socket the peer has
       since dropped is detected and transparently redialed. */
    static HttpResponse* request(char *method, char *url, List<String>* headers, char *body) {
        Url *u = new Url(url);
        defer delete u;

        HttpResponse *r = new HttpResponse();

        String req = Http.buildRequest(method, u, headers, body);

        long  rawlen = 0;
        char *err    = NULL;
        char *raw    = c2m_http_exchange((char *)u->host, u->port, u->secure,
                                         (char *)req, (long)strlen((char *)req),
                                         &rawlen, &err);
        if (raw == NULL) {
            r->error = (err != NULL) ? err : "request failed";
            return r;
        }

        r->ingest(raw, rawlen);
        return r;
    }

    /* GET. */
    static HttpResponse* get(String url) {
        return Http.request("GET", url, NULL, NULL);
    }

    /* GET with extra request headers. */
    static HttpResponse* get(String url, List<String>* headers) {
        return Http.request("GET", url, headers, NULL);
    }

    /* POST a body with a content type (e.g. "application/json"). */
    static HttpResponse* post(char *url, char *contentType, char *body) {
        List<String>* h = new List<String>();
        defer delete h;
        if (contentType != NULL) {
            String ct = "Content-Type: ";
            ct = ct + contentType;
            h->Add(ct);
        }
        return Http.request("POST", url, h, body);
    }

    /* PUT a body with a content type. */
    static HttpResponse* put(char *url, char *contentType, char *body) {
        List<String>* h = new List<String>();
        defer delete h;
        if (contentType != NULL) {
            String ct = "Content-Type: ";
            ct = ct + contentType;
            h->Add(ct);
        }
        return Http.request("PUT", url, h, body);
    }

    /* DELETE. */
    static HttpResponse* del(char *url) {
        return Http.request("DELETE", url, NULL, NULL);
    }

    /* Stream a response body straight to an open FILE* without ever buffering
       the whole thing in memory — ideal for large downloads.  The returned
       HttpResponse carries the status and headers; its `body` is empty (the
       bytes went to `out`) and `length()` reports how many were written.
       Keep-alive pooling applies just like request(). */
    static HttpResponse* download(char *method, char *url, List<String>* headers,
                                  char *body, FILE *out) {
        Url *u = new Url(url);
        defer delete u;

        HttpResponse *r = new HttpResponse();

        String req = Http.buildRequest(method, u, headers, body);

        long  hlen = 0;
        long  blen = 0;
        char *err  = NULL;
        char *hdr  = c2m_http_exchange_to((char *)u->host, u->port, u->secure,
                                          (char *)req, (long)strlen((char *)req),
                                          out, &hlen, &blen, &err);
        if (hdr == NULL) {
            r->error = (err != NULL) ? err : "request failed";
            return r;
        }

        r->ingest(hdr, hlen);
        r->downloaded = blen;
        return r;
    }

    /* GET a URL, streaming the body to `out`. */
    static HttpResponse* download(char *url, FILE *out) {
        return Http.download("GET", url, NULL, NULL, out);
    }
};

#endif /* CLASSYC_HTTPCLIENT_H */
