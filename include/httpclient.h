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
   * The client sends `Connection: close` and reads the whole response, then
     parses it.  Both `Content-Length` and `Transfer-Encoding: chunked` bodies
     are handled.
   * Each HttpResponse owns one heap buffer (the raw response); `statusText`
     and `body` are views into it, freed together by `delete` / `defer delete`.
   * Blocking I/O with a 20s send/recv timeout.  Single-shot request/response;
     no keep-alive, no redirect following (a 3xx is returned as-is so you can
     read `resp->header("location")`).
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

/* Read the connection to EOF into one malloc'd, NUL-terminated buffer. */
static char *c2m_http_read_all(struct c2m_http_conn *c, long *out_len) {
    long  cap = 16384;
    long  len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) { *out_len = 0; return NULL; }

    while (1) {
        if (len + 8192 + 1 > cap) {
            cap = cap * 2;
            char *nb = (char *)realloc(buf, cap);
            if (nb == NULL) { free(buf); *out_len = 0; return NULL; }
            buf = nb;
        }
        long n = c2m_http_conn_read(c, buf + len, 8192);
        if (n <= 0) break;
        len = len + n;
    }
    buf[len] = 0;
    *out_len = len;
    return buf;
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

    HttpResponse() {
        this->status     = 0;
        this->statusText = "";
        this->error      = NULL;
        this->headers    = {};
        this->body       = "";
        this->raw        = NULL;
    }

    ~HttpResponse() {
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

    /* Parse the (JSON) body into a dict. */
    dict asDict() {
        return json((char *)this->body);
    }

    /* Body length in bytes. */
    int length() {
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
    /* Full request.  `headers` is an optional List<String> of "Name: Value"
       lines; `body` is an optional request body.  Always returns a non-NULL
       HttpResponse* (check ->ok() / ->error). */
    static HttpResponse* request(char *method, char *url, List<String>* headers, char *body) {
        Url *u = new Url(url);
        defer delete u;

        HttpResponse *r = new HttpResponse();

        struct c2m_http_conn conn;
        conn.fd  = -1;
        conn.ssl = NULL;

        String portStr = f"{u->port}";
        conn.fd = c2m_http_dial((char *)u->host, (char *)portStr);
        if (conn.fd < 0) {
            r->error = "could not resolve or connect to host";
            return r;
        }

        if (u->secure) {
            if (c2m_http_tls_handshake(&conn, (char *)u->host) != 0) {
                c2m_http_conn_close(&conn);
                r->error = "TLS handshake failed (OpenSSL/libssl required for https)";
                return r;
            }
        }

        /* Build the request line + headers with String concatenation. */
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
        req = req + "Connection: close\r\n\r\n";
        if (body != NULL) req = req + body;

        if (c2m_http_conn_write_all(&conn, (char *)req, (long)strlen((char *)req)) < 0) {
            c2m_http_conn_close(&conn);
            r->error = "failed to send request";
            return r;
        }

        long  rawlen = 0;
        char *raw    = c2m_http_read_all(&conn, &rawlen);
        c2m_http_conn_close(&conn);
        if (raw == NULL) {
            r->error = "failed to read response";
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
};

#endif /* CLASSYC_HTTPCLIENT_H */
