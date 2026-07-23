/* http-serve-fibers.c — fiber-based concurrent HTTP server core.
 *
 * Same server↔app split as http-serve.c, but connections are handled by
 * minicoro fibers with non-blocking sockets on a SINGLE OS thread:
 *
 *   acceptor fiber ──accept──► one connection fiber per client fd
 *
 * Why fibers and not pthreads: the ClassyC String arena is PER-THREAD
 * (_Thread_local) but not fiber-local, so fibers on one OS thread still
 * share that thread's arena.  Cooperative fibers give real request
 * interleaving (recv/send waits overlap) on one OS thread with zero
 * shared-state risk, because app_handle() always runs to completion
 * without yielding.  (Multi-OS-thread servers are now possible — each
 * thread owns its arena — but require app code to hand Strings across
 * threads via detach/attach.)
 *
 * (C11 `_Thread_local` works in ClassyC now — see TLS-IMPLEMENTATION.md —
 * so minicoro uses real TLS for mco_current_co without MCO_PTHREAD_TLS.)
 *
 * Load-bearing hygiene — fibers and the positional String arena:
 * arena checkpoints free by POSITION, so they are only safe across fibers
 * if every fiber yields with NO live arena Strings.  This file keeps that
 * invariant: recv/send loops touch raw malloc'd buffers only, and all
 * Request / Response / json / printf String work happens inside one
 * yield-free block per request.  Applications must follow the same rule:
 * never call mco_yield from inside app_handle().
 *
 * Build (with an app providing main() + app_handle() + ROUTE()s):
 *
 *   ./bin/classyc -I include -I ext/ccchan \
 *       examples/http-serve-fibers.c examples/http_crud/main.cy \
 *       examples/http_crud/items.cy -eg
 *
 * Scope: plain HTTP/1.1 keep-alive, non-blocking sockets, one OS thread.
 */

/* AOT with -ffibers already ships minicoro via mir-aot-runtime.c
   (CHANFIBERS).  Defining MINICORO_IMPL here would multiply-define mco_*.
   JIT loads only this TU's symbols, so we still need the implementation. */
#ifndef CHANFIBERS
#define MINICORO_IMPL
#endif
#include "minicoro.h"

#include "httpserve.h"

/* ── POSIX sockets + fcntl (declared directly, like http-serve.c) ──────── */
extern int  socket(int domain, int type, int protocol);
extern int  setsockopt(int fd, int level, int optname, void *optval, int optlen);
extern int  bind(int fd, void *addr, int addrlen);
extern int  listen(int fd, int backlog);
extern int  accept(int fd, void *addr, void *addrlen);
extern long recv(int fd, void *buf, long len, int flags);
extern long send(int fd, void *buf, long len, int flags);
extern int  close(int fd);
extern int  fcntl(int fd, int cmd, long arg);
extern int  usleep(unsigned int usec);
extern void *signal(int signum, void *handler);
extern unsigned short htons(unsigned short hostshort);
extern int *__errno_location(void);

#define AF_INET       2
#define SOCK_STREAM   1
#define SOL_SOCKET    1
#define SO_REUSEADDR  2
#define IPPROTO_TCP   6
#define TCP_NODELAY   1
#define SIG_IGN_PTR   ((void*)1)
#define SIGPIPE       13
#define F_SETFL       4
#define O_NONBLOCK    2048
#define EAGAIN        11

#define MAX_CONNS     1024
#define CONN_STACK    (64 * 1024)
#define LISTEN_BACKLOG 128

struct sockaddr_in {
    unsigned short sin_family;   /* AF_INET = 2                 */
    unsigned short sin_port;     /* port, network byte order    */
    unsigned int   sin_addr;     /* INADDR_ANY = 0              */
    unsigned char  sin_zero[8];
};

typedef struct {
    mco_coro *co;
    int       cfd;
    int       used;
} conn_slot;

static conn_slot g_conns[MAX_CONNS];
static int       g_listen_fd = -1;

/* Same case-insensitive Content-Length scan as http-serve.c (static per TU). */
static long content_length_of(char *buf, long header_end) {
    const char *key = "content-length:";
    long klen = 15;
    for (long i = 0; i + klen <= header_end; i++) {
        int match = 1;
        for (long j = 0; j < klen; j++) {
            char a = buf[i + j];
            char b = key[j];
            if (a >= 'A' && a <= 'Z') a = a + 32;
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

/* Case-insensitive ASCII equality for len chars (no terminator required). */
static int ci_eq_n(const char *a, const char *b, long n) {
    for (long j = 0; j < n; j++) {
        char x = a[j], y = b[j];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

/* Request-line is HTTP/1.0?  (ApacheBench defaults to HTTP/1.0.) */
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

/* Does the Connection header token-list contain `token` (e.g. "close")? */
static int connection_has(char *buf, long header_end, const char *token) {
    const char *key = "connection:";
    long klen = 11;
    long tlen = 0;
    while (token[tlen]) tlen++;
    for (long i = 0; i + klen <= header_end; i++) {
        if (!ci_eq_n(buf + i, key, klen)) continue;
        /* Only match at start of a header line. */
        if (i > 0 && buf[i - 1] != '\n') continue;
        long k = i + klen;
        while (k < header_end && buf[k] == ' ') k++;
        long vend = k;
        while (vend < header_end && buf[vend] != '\r' && buf[vend] != '\n') vend++;
        /* Scan comma-separated tokens. */
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

/* Should we close after this response?
   HTTP/1.0 defaults to close (unless Connection: keep-alive) — this is what
   ApacheBench sends without -k, and it will hang waiting for EOF if we keep
   the socket open.  HTTP/1.1 defaults to keep-alive unless Connection: close. */
static int wants_close(char *buf, long header_end) {
    if (is_http_10(buf, header_end))
        return !connection_has(buf, header_end, "keep-alive");
    return connection_has(buf, header_end, "close");
}

/* recv that parks the fiber (never the OS thread) while no data is ready.
   Returns >0 bytes read, 0 on orderly close, -1 on error. */
static long nb_recv(int cfd, char *buf, long cap) {
    for (;;) {
        long n = recv(cfd, buf, cap, 0);
        if (n > 0) return n;
        if (n == 0) return 0;
        if (*__errno_location() == EAGAIN) {
            mco_yield(mco_running());
            continue;
        }
        return -1;
    }
}

/* send the whole buffer, parking the fiber on back-pressure.  0 ok, -1 err. */
static int nb_send_all(int cfd, char *buf, long len) {
    long off = 0;
    while (off < len) {
        long n = send(cfd, buf + off, len - off, 0);
        if (n > 0) { off += n; continue; }
        if (n < 0 && *__errno_location() == EAGAIN) {
            mco_yield(mco_running());
            continue;
        }
        return -1;
    }
    return 0;
}

/* Per-connection input accumulator.  Keeps leftover bytes that were read
   ahead past the current request (pipelined next request) across calls. */
typedef struct {
    char *buf;   /* accumulated bytes; buf[len] == 0 invariant */
    long  len;
    long  cap;
} conn_io;

/* Read one full HTTP request from the connection, recycling bytes already
   buffered.  Returns a malloc'd NUL-terminated copy of exactly one request
   and stores its header length in *out_header_end (the body offset must be
   captured BEFORE the caller tokenises — it inserts NULs).  NULL on
   close/error.  Raw memory only — yields freely (no arena Strings here). */
static char *read_request(int cfd, conn_io *io, long *out_header_end) {
    for (;;) {
        if (io->len > 0) {
            char *p = strstr(io->buf, "\r\n\r\n");
            if (p != NULL) {
                long header_end = (long)(p - io->buf) + 4;
                long content_len = content_length_of(io->buf, header_end);
                if (io->len >= header_end + content_len) {
                    long total = header_end + content_len;
                    char *req = (char*) malloc(total + 1);
                    if (req == NULL) return NULL;
                    memcpy(req, io->buf, total);
                    req[total] = 0;
                    /* keep the pipelined remainder for the next request */
                    memmove(io->buf, io->buf + total, io->len - total);
                    io->len -= total;
                    io->buf[io->len] = 0;
                    *out_header_end = header_end;
                    return req;
                }
            }
        }
        if (io->len + 4096 + 1 > io->cap) {
            long ncap = io->cap > 0 ? io->cap * 2 : 8192;
            while (ncap < io->len + 4096 + 1) ncap *= 2;
            char *nb = (char*) realloc(io->buf, ncap);
            if (nb == NULL) return NULL;
            io->buf = nb;
            io->cap = ncap;
        }
        long n = nb_recv(cfd, io->buf + io->len, io->cap - io->len - 1);
        if (n <= 0) return NULL;
        io->len += n;
        io->buf[io->len] = 0;
    }
}

/* Yield-free request zone: parse, dispatch, serialise, log, and release all
   app objects — straight-line so the ownership pass sees the full lifecycle.
   Returns a malloc'd raw response (caller frees after the send loop) and its
   length; NULL means the connection should close without replying. */
static char *handle_one(char *buf, long header_end, int close_after, long *out_len) {
    unowned char *raw = NULL;
    char *method = buf;
    char *sp1 = strchr(buf, ' ');
    if (sp1 != NULL) {
        *sp1 = 0;
        char *target = sp1 + 1;
        char *sp2 = strchr(target, ' ');
        if (sp2 != NULL) *sp2 = 0;

        char *query = "";
        char *q = strchr(target, '?');
        if (q != NULL) { *q = 0; query = q + 1; }
        char *path = target;

        char *body = buf + header_end;   /* captured pre-tokenisation */

        Request *req = new Request(method, path, query, body);
        Response *res = app_handle(req);
        res->keep_alive = close_after ? 0 : 1;

        String msg = res->wire();
        long mlen = (long) strlen((char*) msg);
        /* `unowned`: raw response bytes are copied out of the arena and
           freed by the caller after the (yielding) send loop. */
        raw = (char*) malloc(mlen + 1);
        if (raw != NULL) memcpy(raw, (char*) msg, mlen + 1);

        printf("  %s %s%s%s -> %d\n",
               method, path, query[0] != 0 ? "?" : "", query, res->status);
        fflush(stdout);

        delete res;     /* server owns the Response handed back by app */
        delete req;
        *out_len = mlen;
    }
    free(buf);
    return raw;
}

/* One fiber per connection: serve requests until the client closes or asks
   for Connection: close (HTTP/1.1 keep-alive by default). */
static void conn_fiber(mco_coro *co) {
    conn_slot *slot = (conn_slot *) mco_get_user_data(co);
    int cfd = slot->cfd;

    conn_io io;
    io.buf = NULL; io.len = 0; io.cap = 0;

    for (;;) {
        long header_end = 0;
        /* `unowned`: raw malloc memory freed by hand below / in handle_one —
           manual lifetime across the keep-alive loop. */
        unowned char *buf = read_request(cfd, &io, &header_end);
        if (buf == NULL) break;
        int close_after = wants_close(buf, header_end);

        long mlen = 0;
        unowned char *raw = handle_one(buf, header_end, close_after, &mlen);
        if (raw == NULL) break;
        /* ═══ end yield-free zone; only raw malloc memory crosses ═══ */
        int rc = nb_send_all(cfd, raw, mlen);
        free(raw);
        if (rc < 0) break;
        if (close_after) break;
    }
    close(cfd);
    if (io.buf) free(io.buf);
    /* Do NOT clear the slot here: the scheduler reaps the fiber once it
       reports MCO_DEAD (mco_destroy frees the 64KB stack).  Clearing co
       ourselves would leak every connection's stack. */
    slot->cfd = -1;
}

/* Accept loop: parks on EAGAIN, spawns one conn fiber per client. */
static void accept_fiber(mco_coro *co) {
    (void) co;
    for (;;) {
        int cfd = accept(g_listen_fd, 0, 0);
        if (cfd < 0) {
            mco_yield(mco_running());
            continue;
        }
        fcntl(cfd, F_SETFL, O_NONBLOCK);
        {
            int one = 1;
            setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, 4);
        }

        int i;
        for (i = 0; i < MAX_CONNS; i++) if (!g_conns[i].used) break;
        if (i == MAX_CONNS) { close(cfd); continue; }

        mco_desc desc = mco_desc_init(conn_fiber, CONN_STACK);
        desc.user_data = &g_conns[i];
        mco_coro *nco = 0;
        if (mco_create(&nco, &desc) != MCO_SUCCESS) {
            close(cfd);
            continue;
        }
        g_conns[i].used = 1;
        g_conns[i].cfd  = cfd;
        g_conns[i].co   = nco;
    }
}

/* serve_fibers — bind, listen, and run the fiber scheduler forever. */
int serve_fibers(int port) {
    signal(SIGPIPE, SIG_IGN_PTR);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { printf("socket() failed\n"); return 1; }

    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, 4);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short) port);
    addr.sin_addr   = 0;

    if (bind(sfd, &addr, 16) < 0) {
        printf("bind() failed (port %d in use?)\n", port);
        close(sfd);
        return 1;
    }
    if (listen(sfd, LISTEN_BACKLOG) < 0) {
        printf("listen() failed\n");
        close(sfd);
        return 1;
    }
    fcntl(sfd, F_SETFL, O_NONBLOCK);
    g_listen_fd = sfd;

    mco_coro *acc = 0;
    mco_desc desc = mco_desc_init(accept_fiber, CONN_STACK);
    if (mco_create(&acc, &desc) != MCO_SUCCESS) {
        printf("mco_create() failed\n");
        close(sfd);
        return 1;
    }

    printf("ClassyC http-serve-fibers listening on http://127.0.0.1:%d\n", port);
    fflush(stdout);

    /* Single-OS-thread scheduler: resume every suspended fiber round-robin.
       The accept fiber is almost always suspended (parks on EAGAIN), so it
       must NOT count as "busy" — otherwise we never sleep and burn 100% CPU
       on an idle server (and SIGTERM teardown can look like a hang).
       Sleep when there are no live connection fibers. */
    for (;;) {
        int live = 0;

        if (mco_status(acc) == MCO_SUSPENDED)
            mco_resume(acc);

        for (int i = 0; i < MAX_CONNS; i++) {
            if (!g_conns[i].used || g_conns[i].co == 0) continue;
            if (mco_status(g_conns[i].co) == MCO_SUSPENDED)
                mco_resume(g_conns[i].co);
            if (mco_status(g_conns[i].co) == MCO_DEAD) {
                mco_destroy(g_conns[i].co);
                g_conns[i].co   = 0;
                g_conns[i].used = 0;
                g_conns[i].cfd  = -1;
                continue;
            }
            live++;
        }
        if (live == 0)
            usleep(1000);
    }
    /* not reached */
    return 0;
}
