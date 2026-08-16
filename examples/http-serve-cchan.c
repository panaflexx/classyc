/* http-serve-cchan.c — multi-OS-thread HTTP server core using ext/ccchan's
 * cchan_t directly (pthreads only — no coroutines, no -ffibers, no minicoro).
 *
 * Same server<->app contract as http-serve.c / http-serve-fibers.c:
 *   app provides main() + app_handle() + [[HttpGet]] / ROUTE()s
 *   this TU provides serve_workers_cchan(port, nworkers)
 *
 * Model:
 *
 *   acceptor (main OS thread)  --cfd-->  cchan_t (int32)  --> N pthread workers
 *
 * Each worker blocks on cchan_recv_i32, handles one client to completion on
 * its own OS thread/stack, then loops for the next fd. Plain thread-pool
 * semantics: a slow/blocking client stalls only the worker that picked it up.
 *
 * nworkers > 1 means real parallel execution of app_handle() across OS
 * threads. That's fine for a stateless app or one whose own data structures
 * are already synchronized; an app with shared mutable state and no locking
 * of its own needs to add that locking (e.g. classyc-db-server.cy wraps its
 * app_handle() body in a single pthread_mutex_t around all Database access —
 * see that file's own comment) or otherwise pass nworkers=1 for
 * cooperative single-thread semantics in that case.
 *
 * Why this exists alongside http-serve-fibers.c: that file (and only that
 * file, among the fiber-flavored options here) #includes ext/ccchan/
 * minicoro.h directly, and classyc's parser doesn't yet accept the
 * hand-written __asm__ block partway through it. http-serve-workers.cy's
 * go/Chan<T> pool (-ffibers) does NOT hit this: its coroutine plumbing
 * (cyfiber.h's CYFIBER_IMPLEMENTATION block) is compiled once into the
 * classyc driver/AOT runtime by a real C compiler, never parsed from a .cy
 * TU. cchan.h alone (no CCHAN_IMPLEMENTATION coroutine bits) has no such
 * dependency either, so this file is here mainly for apps that want a
 * worker pool without opting into -ffibers at all.
 *
 * Build (with an app providing main + app_handle + routes):
 *
 *   ./bin/classyc -I include -I ext/ccchan -w \
 *       examples/http-serve.c examples/http-serve-cchan.c \
 *       <app>.cy -eg
 *
 * (-w quiets false-positive ownership warnings the analyzer raises walking
 * cchan.h's own free paths, same as classy-cchan.cy.)
 */
#define CCHAN_IMPLEMENTATION
#include "cchan.h"
#include "httpserve.h"
#include <pthread.h>

extern int accept(int fd, void *addr, void *addrlen);
extern int close(int fd);

static void *http_worker_loop(void *arg) {
    cchan_t *jobs = (cchan_t *) arg;
    int cfd = 0;
    while (cchan_recv_i32(jobs, &cfd) == 1) {
        http_handle_client(cfd);
        close(cfd);
    }
    return 0;
}

/* serve_workers_cchan — bind, start N OS worker threads, fan accepted fds
   over a bounded cchan_t. */
int serve_workers_cchan(int port, int nworkers) {
    if (nworkers < 1) nworkers = 1;
    if (nworkers > 32) nworkers = 32;

    int sfd = http_listen(port);
    if (sfd < 0) return 1;

    cchan_t *jobs = cchan_create(256, sizeof(int));
    if (jobs == 0) { close(sfd); return 1; }

    pthread_t workers[32];
    for (int i = 0; i < nworkers; i++)
        pthread_create(&workers[i], 0, http_worker_loop, jobs);

    printf("ClassyC http-serve-cchan listening on http://127.0.0.1:%d  "
           "(%d OS workers, cchan fan-out)\n", port, nworkers);
    fflush(stdout);

    while (1) {
        int cfd = accept(sfd, 0, 0);
        if (cfd < 0) continue;
        cchan_send_i32(jobs, cfd);
    }
    /* not reached */
    cchan_dispose(jobs);
    close(sfd);
    return 0;
}
