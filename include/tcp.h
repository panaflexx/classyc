/* =========================================================================
   include/tcp.h — minimal TCP server/client for ClassyC programs
   =========================================================================

   Adapted from nanoproxy/include/socket_server.h — stripped to the
   essentials needed for single-client protocol servers like DAP.

   ── Usage (server) ─────────────────────────────────────────────────────

   int srv = tcp_listen("127.0.0.1", 4711, 1);
   int client = tcp_accept(srv);
   char buf[4096];
   int n = tcp_read(client, buf, sizeof(buf));
   tcp_write(client, "hello\n", 6);
   tcp_close(client);
   tcp_close(srv);

   ── Usage (client) ─────────────────────────────────────────────────────

   int fd = tcp_connect("127.0.0.1", 4711);
   tcp_write(fd, "GET / HTTP/1.0\r\n\r\n", 18);
   char buf[4096];
   int n = tcp_read(fd, buf, sizeof(buf));
   tcp_close(fd);

   ── Design ─────────────────────────────────────────────────────────────

   Header-only, no dependencies beyond POSIX sockets.
   All functions return -1 on error with errno set.
   Blocking I/O by default — appropriate for single-client protocols.
   tcp_set_nonblocking() available when needed.
   ========================================================================= */

#ifndef TCP_H
#define TCP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── POSIX socket declarations (avoid pulling full headers in ClassyC) ─ */

/* Address families */
#define TCP_AF_INET    2
#define TCP_AF_INET6   10

/* Socket types */
#define TCP_SOCK_STREAM 1

/* SOL / SO constants */
#define TCP_SOL_SOCKET  1
#define TCP_SO_REUSEADDR 2
#define TCP_SO_KEEPALIVE 9

/* IPPROTO_TCP / TCP_NODELAY */
#define TCP_IPPROTO_TCP 6
#define TCP_TCP_NODELAY 1

/* Socket address structures — layout-compatible with POSIX */

struct tcp_sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int   sin_addr;
    char           sin_zero[8];
};

struct tcp_sockaddr_in6 {
    unsigned short sin6_family;
    unsigned short sin6_port;
    unsigned int   sin6_flowinfo;
    unsigned char  sin6_addr[16];
    unsigned int   sin6_scope_id;
};

/* Generic sockaddr (large enough for both v4 and v6) */
struct tcp_sockaddr_storage {
    unsigned short ss_family;
    char           _pad[126];
};

/* ── POSIX function declarations ──────────────────────────────────────── */

extern int    socket(int domain, int type, int protocol);
extern int    bind(int fd, void *addr, int addrlen);
extern int    listen(int fd, int backlog);
extern int    accept(int fd, void *addr, int *addrlen);
extern int    connect(int fd, void *addr, int addrlen);
extern int    setsockopt(int fd, int level, int optname, void *optval, int optlen);
extern int    getsockname(int fd, void *addr, int *addrlen);
extern long   send(int fd, void *buf, long len, int flags);
extern long   recv(int fd, void *buf, long len, int flags);
extern int    close(int fd);
extern int    shutdown(int fd, int how);

/* fcntl for non-blocking */
extern int    fcntl(int fd, int cmd, ...);

/* htons / htonl — byte order conversion (local, LE hosts).
   Avoids libc htons which on Darwin expands to OSSwapInt16 (not a
   stable AOT/JIT import). */
static unsigned short htons(unsigned short hostshort) {
    return (unsigned short)((hostshort << 8) | (hostshort >> 8));
}
static unsigned int htonl(unsigned int hostlong) {
    return ((hostlong & 0xff) << 24) | ((hostlong & 0xff00) << 8)
         | ((hostlong >> 8) & 0xff00) | ((hostlong >> 24) & 0xff);
}
static unsigned short ntohs(unsigned short netshort) { return htons(netshort); }
static unsigned int   ntohl(unsigned int netlong)   { return htonl(netlong); }

/* inet_pton — address string to binary */
extern int inet_pton(int af, char *src, void *dst);
/* inet_ntop — binary to address string */
extern char *inet_ntop(int af, void *src, char *dst, int size);

/* ══════════════════════════════════════════════════════════════════════
   TCP API
   ══════════════════════════════════════════════════════════════════════ */

/* Set a file descriptor to non-blocking mode.
   Returns 0 on success, -1 on error. */
static int tcp_set_nonblocking(int fd) {
    int flags = fcntl(fd, 3 /* F_GETFL */, 0);  /* F_GETFL = 3 */
    if (flags < 0) return -1;
    return fcntl(fd, 4 /* F_SETFL */, flags | 2048 /* O_NONBLOCK */);
}

/* Create a TCP server socket, bind to host:port, and listen.
   host may be "0.0.0.0" for all interfaces, or "127.0.0.1" for localhost.
   backlog is the listen queue depth (typically 1-128).
   Returns the listening socket fd, or -1 on error. */
static int tcp_listen(char *host, int port, int backlog) {
    int fd = socket(TCP_AF_INET, TCP_SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* SO_REUSEADDR — allow quick restart */
    int opt = 1;
    setsockopt(fd, TCP_SOL_SOCKET, TCP_SO_REUSEADDR, (void *)&opt, sizeof(opt));

    struct tcp_sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = TCP_AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (host && strcmp(host, "0.0.0.0") != 0) {
        inet_pton(TCP_AF_INET, host, (void *)&addr.sin_addr);
    }
    /* else sin_addr stays 0 = INADDR_ANY */

    if (bind(fd, (void *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, backlog > 0 ? backlog : 1) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* Accept one incoming connection (blocks until a client connects).
   Returns the connected client fd, or -1 on error. */
static int tcp_accept(int listen_fd) {
    struct tcp_sockaddr_storage peer;
    int peer_len = sizeof(peer);
    memset(&peer, 0, sizeof(peer));

    int fd = accept(listen_fd, (void *)&peer, &peer_len);
    if (fd < 0) return -1;

    /* TCP_NODELAY — disable Nagle for low-latency protocol messages */
    int opt = 1;
    setsockopt(fd, TCP_IPPROTO_TCP, TCP_TCP_NODELAY, (void *)&opt, sizeof(opt));

    return fd;
}

/* Connect to a remote TCP server.
   Returns the connected socket fd, or -1 on error. */
static int tcp_connect(char *host, int port) {
    int fd = socket(TCP_AF_INET, TCP_SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct tcp_sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = TCP_AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (inet_pton(TCP_AF_INET, host, (void *)&addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (void *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* TCP_NODELAY */
    int opt = 1;
    setsockopt(fd, TCP_IPPROTO_TCP, TCP_TCP_NODELAY, (void *)&opt, sizeof(opt));

    return fd;
}

/* Get the port a listening socket is actually bound to.
   Useful when tcp_listen() was called with port 0 (OS-assigned).
   Returns the port number, or -1 on error. */
static int tcp_bound_port(int fd) {
    struct tcp_sockaddr_in addr;
    int len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(fd, (void *)&addr, &len) < 0) return -1;
    return (int)ntohs(addr.sin_port);
}

/* Read up to `len` bytes into `buf`.  Blocks until data is available.
   Returns bytes read (>0), 0 on clean disconnect, -1 on error. */
static int tcp_read(int fd, char *buf, int len) {
    long n = recv(fd, (void *)buf, (long)len, 0);
    return (int)n;
}

/* Read exactly `len` bytes into `buf`, looping as needed.
   Returns `len` on success, or -1 if the connection drops before
   all bytes are received. */
static int tcp_read_exact(int fd, char *buf, int len) {
    int total = 0;
    while (total < len) {
        long n = recv(fd, (void *)(buf + total), (long)(len - total), 0);
        if (n <= 0) return -1;
        total = total + (int)n;
    }
    return total;
}

/* Write exactly `len` bytes from `buf`, looping as needed.
   Returns `len` on success, or -1 on error. */
static int tcp_write(int fd, char *buf, int len) {
    int total = 0;
    while (total < len) {
        long n = send(fd, (void *)(buf + total), (long)(len - total), 0);
        if (n <= 0) return -1;
        total = total + (int)n;
    }
    return total;
}

/* Graceful shutdown + close. */
static void tcp_close(int fd) {
    if (fd >= 0) {
        shutdown(fd, 2 /* SHUT_RDWR */);
        close(fd);
    }
}

#endif /* TCP_H */
