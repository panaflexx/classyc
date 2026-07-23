/* http-serve-workers.cy — multi-OS-thread HTTP server core (needs -ffibers).
 *
 * Same server↔app contract as http-serve.c / http-serve-fibers.c:
 *   app provides main() + app_handle() + [[HttpGet]] / ROUTE()s
 *   this TU provides serve_workers(port, nworkers)
 *
 * Model:
 *
 *   acceptor (main OS thread)  ──cfd──►  Chan<int>  ──►  N worker fibers
 *                                                       each pinned to an OS
 *                                                       thread by cy_sched
 *
 * Only the bare int fd rides the channel.  String arenas are per-OS-thread
 * (_Thread_local); each worker runs http_handle_client → close on its own
 * arena.  Blocking socket I/O stalls that worker thread (thread-pool
 * semantics) — use serve_fibers() for single-thread I/O concurrency.
 *
 * Build (with an app providing main + app_handle + routes):
 *
 *   ./bin/classyc -I include -I ext/ccchan -ffibers -l sqlite3 \
 *       examples/http-serve.c examples/http-serve-fibers.c \
 *       examples/http-serve-workers.cy \
 *       examples/http_crud/main.cy examples/http_crud/items.cy \
 *       -eg -- workers --workers=4
 */

#include "httpserve.h"
#include "chan.h"
#include <stdio.h>

extern int  accept(int fd, void *addr, void *addrlen);
extern int  close(int fd);

static void http_worker_loop(Chan<int> *jobs) {
    int cfd = 0;
    while (jobs->recv(&cfd)) {
        http_handle_client(cfd);
        close(cfd);
    }
}

/* serve_workers — bind, start N OS workers, fan accepted fds over a channel. */
int serve_workers(int port, int nworkers) {
    if (nworkers < 1) nworkers = 1;
    if (nworkers > 32) nworkers = 32;

    int sfd = http_listen(port);
    if (sfd < 0) return 1;

    /* Start the OS worker pool first so every `go` pins onto a real worker
       (and never runs on the acceptor thread). */
    cy_sched_init(nworkers);

    /* Lives for the process lifetime (accept loop never returns). */
    unowned Chan<int> *jobs = new Chan<int>(256);
    for (int i = 0; i < nworkers; i++)
        go http_worker_loop(jobs);

    printf("ClassyC http-serve-workers listening on http://127.0.0.1:%d  "
           "(%d OS workers, Chan fan-out)\n", port, nworkers);
    fflush(stdout);

    for (;;) {
        int cfd = accept(sfd, 0, 0);
        if (cfd < 0) continue;
        /* Parks main if the inbox is full (workers busy); cy_yield outside a
           fiber just usleeps in multi-worker mode while the pool drains. */
        jobs->send(cfd);
    }
    /* not reached */
    return 0;
}
