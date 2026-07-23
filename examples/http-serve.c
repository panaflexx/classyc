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
 * Scope: plain HTTP/1.1, one request per connection (Connection: close), one
 * connection at a time.  That keeps the example focused on the request/response
 * object model and the server↔app split rather than on an event loop.
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
extern unsigned short htons(unsigned short hostshort);

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
#define SIG_IGN_PTR   ((void*)1)
#define SIGPIPE       13

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

/* Read one HTTP request off the socket, dispatch it through app_handle(), and
   write the response back.  Tokenises the request in place.
   Does not close cfd — callers (serve loop / worker pool) own the fd. */
void http_handle_client(int cfd) {
    long cap = 8192, len = 0;
    char *buf = (char *) malloc(cap);
    if (buf == NULL) return;

    long header_end = -1;
    long content_len = 0;

    /* Read until we have the full header block and the declared body. */
    while (1) {
        if (len + 1 >= cap) {
            cap = cap * 2;
            buf = (char *) realloc(buf, cap);
            if (buf == NULL) return;
        }
        long n = recv(cfd, buf + len, cap - len - 1, 0);
        if (n <= 0) break;
        len = len + n;
        buf[len] = 0;
        if (header_end < 0) {
            char *p = strstr(buf, "\r\n\r\n");
            if (p != NULL) {
                header_end = (long)(p - buf) + 4;
                content_len = content_length_of(buf, header_end);
            }
        }
        if (header_end >= 0 && len >= header_end + content_len) break;
    }

    if (header_end < 0) { free(buf); return; }   /* malformed / closed early */

    /* Request line:  METHOD SP target SP HTTP/1.1  — tokenise in place. */
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

    char *body = buf + header_end;   /* NUL-terminated (buf[len] = 0) */

    /* Build the Request, run the app, serialise + send the Response. */
    Request *req = new Request(method, path, query, body);
    Response *res = app_handle(req);

    String msg = res->wire();
    send(cfd, (char *) msg, (long) strlen((char *) msg), 0);

    printf("  %s %s%s%s -> %d\n",
           (char *) method, (char *) path,
           query[0] != 0 ? "?" : "", (char *) query,
           res->status);
    fflush(stdout);

    delete res;     /* server owns the Response handed back by app_handle */
    delete req;
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
    addr.sin_port   = htons((unsigned short) port);
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
