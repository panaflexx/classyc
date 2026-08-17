/* http-serve.c — the ClassyC "gunicorn-like" base HTTP server.
 *
 * This translation unit has NO main().  It implements `serve(port)` (declared
 * in httpserve.h): a single-worker, blocking accept loop that parses each HTTP
 * request into a `Request`, hands it to the application's `app_handle()`, and
 * writes the returned `Response` back to the socket.
 *
 * Compile it together with an application that provides main() + app_handle():
 *
 *     ./bin/classyc -I include -l crypto -l sqlite \
 *                   examples/http-serve.c examples/classy-http-app.c -eg
 *
 * Then, in another terminal:
 *
 *     curl -s http://127.0.0.1:8080/api/users
 *     curl -s -X POST -d '{"name":"Dave","email":"dave@x.com"}' \
 *          http://127.0.0.1:8080/api/users
 *
 * Scope: HTTP/1.1 keep-alive (HTTP/1.0 needs Connection: keep-alive).
 * Browsers and `ab -k` reuse the socket; Connection: close or idle timeout
 * ends it.  Apps still just return a Response — the core sets keep_alive.
 */
#include "httpserve.h"

/* ── POSIX server sockets (declared directly, à la include/httpclient.h) ─── */
extern int  socket(int domain, int type, int protocol);
extern int  setsockopt(int fd, int level, int optname, void *optval, int optlen);
extern int  bind(int fd, void *addr, int addrlen);
extern int  listen(int fd, int backlog);
extern int  accept(int fd, void *addr, void *addrlen);
extern long recv(int fd, void *buf, long len, int flags);
extern long send(int fd, void *buf, long len, int flags);
extern int  close(int fd);
extern void *signal(int signum, void *handler);

/* Avoid libc htons (Darwin maps it to OSSwapInt16, not a useful AOT symbol). */
static unsigned short cy_htons(unsigned short x) {
    return (unsigned short)((x << 8) | (x >> 8));
}

/* IPv4 socket address (16 bytes on LP64); we only set a few fields. */
struct sockaddr_in {
    unsigned short sin_family;   /* AF_INET = 2                 */
    unsigned short sin_port;     /* port, network byte order    */
    unsigned int   sin_addr;     /* INADDR_ANY = 0              */
    unsigned char  sin_zero[8];
};

#define AF_INET       2
#define SOCK_STREAM   1
#define SOL_SOCKET    1
#define SO_REUSEADDR  2
#if defined(__APPLE__)
#define SO_RCVTIMEO   0x1006
#else
#define SO_RCVTIMEO   20
#endif
#define SIG_IGN_PTR   ((void*)1)
#define SIGPIPE       13
#define HTTP_BUF_MAX  (1 << 20)

struct cy_timeval {
    long tv_sec;
    long tv_usec;
};

/* Locate the Content-Length value within the header block (case-insensitive).
   Returns 0 when the header is absent. */
static long content_length_of(char *buf, long header_end) {
    const char *key = "content-length:";
    long klen = 15;
    for (long i = 0; i + klen <= header_end; i++) {
        int match = 1;
        for (long j = 0; j < klen; j++) {
            char a = buf[i + j];
            char b = key[j];
            if (a >= 'A' && a <= 'Z') a = a + 32;   /* ASCII tolower */
            if (a != b) { match = 0; break; }
        }
        if (!match) continue;
        long k = i + klen;
        while (k < header_end && buf[k] == ' ') k++;
        long v = 0;
        while (k < header_end && buf[k] >= '0' && buf[k] <= '9') {
            v = v * 10 + (buf[k] - '0');
            k++;
        }
        return v;
    }
    return 0;
}

static int ci_eq_n(const char *a, const char *b, long n) {
    for (long j = 0; j < n; j++) {
        char x = a[j], y = b[j];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

static int is_http_10(char *buf, long header_end) {
    long line_end = 0;
    while (line_end < header_end && !(buf[line_end] == '\r' && line_end + 1 < header_end
                                      && buf[line_end + 1] == '\n'))
        line_end++;
    for (long i = 0; i + 8 <= line_end; i++) {
        if (ci_eq_n(buf + i, "HTTP/1.0", 8)) return 1;
    }
    return 0;
}

static int connection_has(char *buf, long header_end, const char *token) {
    const char *key = "connection:";
    long klen = 11;
    long tlen = 0;
    while (token[tlen]) tlen++;
    for (long i = 0; i + klen <= header_end; i++) {
        if (!ci_eq_n(buf + i, key, klen)) continue;
        if (i > 0 && buf[i - 1] != '\n') continue;
        long k = i + klen;
        while (k < header_end && buf[k] == ' ') k++;
        long vend = k;
        while (vend < header_end && buf[vend] != '\r' && buf[vend] != '\n') vend++;
        long p = k;
        while (p < vend) {
            while (p < vend && (buf[p] == ' ' || buf[p] == ',' || buf[p] == '\t')) p++;
            long q = p;
            while (q < vend && buf[q] != ',' && buf[q] != ' ' && buf[q] != '\t') q++;
            if (q - p == tlen && ci_eq_n(buf + p, token, tlen)) return 1;
            p = q;
        }
        return 0;
    }
    return 0;
}

/* HTTP/1.0 defaults to close (unless Connection: keep-alive) — ab without -k
   hangs if we leave the socket open.  HTTP/1.1 keep-alive unless close. */
static int wants_close(char *buf, long header_end) {
    if (is_http_10(buf, header_end))
        return !connection_has(buf, header_end, "keep-alive");
    return connection_has(buf, header_end, "close");
}

static int env_int(const char *name, int fallback) {
    char *e = getenv(name);
    if (e == 0 || e[0] == 0) return fallback;
    return atoi(e);
}

/* Serve one connection: HTTP/1.1 keep-alive (and HTTP/1.0 + keep-alive).
   Does not close cfd — callers own the fd.  Idle recv timeout (default 5s,
   HTTP_KEEPALIVE_TIMEOUT) frees a worker if the peer goes quiet. */
void http_handle_client(int cfd) {
    long cap = 8192, len = 0;
    /* unowned: one malloc spanning the keep-alive loop; freed on every exit. */
    unowned char *buf = (char *) malloc(cap);
    if (buf == NULL) return;

    int idle = env_int("HTTP_KEEPALIVE_TIMEOUT", 5);
    if (idle < 1) idle = 1;
    int max_req = env_int("HTTP_KEEPALIVE_MAX", 1000);
    if (max_req < 1) max_req = 1;
    {
        struct cy_timeval tv;
        tv.tv_sec = idle;
        tv.tv_usec = 0;
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, (int) sizeof(tv));
    }

    int nserved = 0;
    while (nserved < max_req) {
        long header_end = -1;
        long content_len = 0;

        while (1) {
            if (header_end < 0 && len > 0) {
                char *p = strstr(buf, "\r\n\r\n");
                if (p != NULL) {
                    header_end = (long)(p - buf) + 4;
                    content_len = content_length_of(buf, header_end);
                }
            }
            if (header_end >= 0 && len >= header_end + content_len) break;

            if (len + 1 >= cap) {
                if (cap >= HTTP_BUF_MAX) { free(buf); return; }
                cap = cap * 2;
                if (cap > HTTP_BUF_MAX) cap = HTTP_BUF_MAX;
                buf = (char *) realloc(buf, cap);
                if (buf == NULL) return;
            }
            long n = recv(cfd, buf + len, cap - len - 1, 0);
            if (n <= 0) { free(buf); return; }  /* peer close or idle timeout */
            len = len + n;
            buf[len] = 0;
        }

        int close_after = wants_close(buf, header_end);
        if (nserved + 1 >= max_req) close_after = 1;

        char *method = buf;
        char *sp1 = strchr(buf, ' ');
        if (sp1 == NULL) { free(buf); return; }
        *sp1 = 0;
        char *target = sp1 + 1;
        char *sp2 = strchr(target, ' ');
        if (sp2 != NULL) *sp2 = 0;

        char *query = "";
        char *q = strchr(target, '?');
        if (q != NULL) { *q = 0; query = q + 1; }
        char *path = target;
        char *body = buf + header_end;

        unowned Request *req = new Request(method, path, query, body);
        unowned Response *res = app_handle(req);
        if (res == 0) { delete req; free(buf); return; }
        res->keep_alive = close_after ? 0 : 1;

        String msg = res->wire();
        send(cfd, (char *) msg, (long) strlen((char *) msg), 0);

        if (getenv("HTTP_QUIET") == 0) {
            printf("  %s %s%s%s -> %d%s\n",
                   (char *) method, (char *) path,
                   query[0] != 0 ? "?" : "", (char *) query,
                   res->status,
                   res->keep_alive ? "  ka" : "");
            fflush(stdout);
        }

        delete res;
        delete req;
        nserved++;

        if (close_after) break;

        /* Pipelined remainder stays in the buffer for the next request. */
        long used = header_end + content_len;
        long rest = len - used;
        if (rest > 0) memmove(buf, buf + used, (size_t) rest);
        len = rest;
        buf[len] = 0;
    }
    free(buf);
}

/* http_listen — bind + listen on 0.0.0.0:port.  Returns the listen fd, or -1. */
int http_listen(int port) {
    signal(SIGPIPE, SIG_IGN_PTR);   /* a peer that hung up must not kill us */

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { printf("socket() failed\n"); return -1; }

    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, 4);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = cy_htons((unsigned short) port);
    addr.sin_addr   = 0;            /* INADDR_ANY */

    if (bind(sfd, &addr, 16) < 0) {
        printf("bind() failed (port %d in use?)\n", port);
        close(sfd);
        return -1;
    }
    if (listen(sfd, 128) < 0) {
        printf("listen() failed\n");
        close(sfd);
        return -1;
    }
    return sfd;
}

/* serve — single-worker blocking accept loop. */
int serve(int port) {
    int sfd = http_listen(port);
    if (sfd < 0) return 1;

    printf("ClassyC http-serve listening on http://127.0.0.1:%d\n", port);
    fflush(stdout);

    while (1) {
        int cfd = accept(sfd, 0, 0);
        if (cfd < 0) continue;
        http_handle_client(cfd);
        close(cfd);
    }
    /* not reached */
    close(sfd);
    return 0;
}
