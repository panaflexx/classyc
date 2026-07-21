/*
 * fiber_workers.c — continuous order processing: N fibers on M OS threads + cchan
 *
 * Default: 500 fibers · 8 workers · run 60s · main prints stats every 1s.
 *
 *   traders (fibers) ──orders──► matchers (fibers) ──fills──► (counted)
 *        pinned across M OS threads (minicoro per-thread)
 *        cchan carries messages across threads
 *
 * Rules:
 *   - Each fiber stays on one OS thread (never mco_resume across Ms)
 *   - Fibers: cchan_try_* + mco_yield (never block the local scheduler)
 *   - Main: sleeps 1s, prints rates; after duration sets stop and joins
 *
 * Build:
 *   make fiber-workers
 *   ./fiber_workers
 *   ./fiber_workers --seconds=10 --fibers=500 --workers=8
 *   ./fiber_workers --quick
 */

#define MINICORO_IMPL
#include "minicoro.h"

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static int g_fail = 0;
static int g_quick = 0;
static int g_verbose = 0;
static int g_nfibers = 500;
static int g_nworkers = 8;
static int g_seconds = 60;

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
    g_fail++; \
} while (0)

#define INFO(fmt, ...) do { \
    printf("  " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)

#define HEAD(fmt, ...) do { \
    printf("\n=== " fmt " ===\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)

#define MAX_WORKERS      64
#define MAX_FIBERS_TOTAL 4096
#define MAX_LOCAL        256
#define MAX_BOOK         64

/* -------------------------------------------------------------------------- */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int fiber_send(cchan_t* c, const void* msg, int max_yields)
{
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_send(c, msg);
        if (rc == 1) return 1;
        if (rc < 0) return 0;
        if (y < max_yields) {
            mco_coro* self = mco_running();
            if (!self) return 0;
            mco_yield(self);
        }
    }
    return 0;
}

static int fiber_recv(cchan_t* c, void* msg, int max_yields)
{
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_recv(c, msg);
        if (rc == 1) return 1;
        if (rc < 0) return 0;
        if (y < max_yields) {
            mco_coro* self = mco_running();
            if (!self) return 0;
            mco_yield(self);
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* order protocol                                                             */
/* -------------------------------------------------------------------------- */

typedef enum {
    ORD_NEW = 1,
    ORD_FILL,
    ORD_REJECT,
    ORD_SHUTDOWN
} ord_type;

typedef struct {
    int32_t type;
    int32_t trader_id;   /* fiber global id of trader */
    int32_t order_id;
    int32_t side;        /* 0 buy 1 sell */
    int32_t price;
    int32_t qty;
    int32_t fill_qty;
    int32_t pad;
} order_t;

typedef struct {
    int active;
    int order_id;
    int trader_id;
    int side;
    int price;
    int qty_left;
} book_ent;

/* Shared queues (cross-thread). */
static cchan_t* g_order_q;   /* traders → matchers */
static cchan_t* g_fill_q;    /* matchers → sinks (count only) */

static atomic_int g_stop;
static atomic_int g_next_oid;

static atomic_ullong g_orders_sent;
static atomic_ullong g_orders_matched;   /* match events (can be >1 per order) */
static atomic_ullong g_orders_filled;    /* fill messages */
static atomic_ullong g_orders_rejected;
static atomic_ullong g_fill_qty_total;
static atomic_ullong g_book_rests;

/* -------------------------------------------------------------------------- */
/* worker + fiber setup                                                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    int worker_id;
    int nlocal;
    mco_coro* cos[MAX_LOCAL];
    int alive[MAX_LOCAL];
} worker_t;

static worker_t g_workers[MAX_WORKERS];

typedef struct {
    int global_id;
    int worker_id;
    int is_trader;   /* 1 trader, 0 matcher */
} fiber_ctx;

static fiber_ctx g_fctx[MAX_FIBERS_TOTAL];

/* -------------------------------------------------------------------------- */
/* trader fiber — generate orders until stop                                  */
/* -------------------------------------------------------------------------- */

static void trader_fiber(mco_coro* co)
{
    fiber_ctx* cx = (fiber_ctx*)mco_get_user_data(co);
    int seq = 0;
    unsigned burst = 0;

    while (!atomic_load(&g_stop)) {
        order_t o;
        memset(&o, 0, sizeof(o));
        o.type = ORD_NEW;
        o.trader_id = cx->global_id;
        o.order_id = atomic_fetch_add(&g_next_oid, 1);
        o.side = (seq + cx->global_id) & 1;
        o.price = 1000 + ((seq * 3 + cx->global_id * 7) % 7) - 3;
        o.qty = 1 + (seq % 4);

        if (!fiber_send(g_order_q, &o, 64)) {
            /* full or closed — yield and retry unless stopping */
            if (atomic_load(&g_stop))
                break;
            mco_yield(co);
            continue;
        }
        atomic_fetch_add(&g_orders_sent, 1);
        seq++;
        burst++;
        /* keep the local matcher(s) and other traders scheduled */
        if ((burst & 3u) == 3u)
            mco_yield(co);
    }
    if (g_verbose)
        INFO("trader %d exit after %d orders", cx->global_id, seq);
}

/* -------------------------------------------------------------------------- */
/* matcher fiber — continuous book until stop + queue drain                   */
/* -------------------------------------------------------------------------- */

static void match_one(book_ent* book, int bookn, order_t* o)
{
    int i;
    int remaining = o->qty;

    for (i = 0; i < bookn && remaining > 0; i++) {
        book_ent* b = &book[i];
        int fq;
        if (!b->active) continue;
        if (b->side == o->side) continue;
        if (o->side == 0) {
            if (o->price < b->price) continue;
        } else {
            if (o->price > b->price) continue;
        }

        fq = remaining < b->qty_left ? remaining : b->qty_left;

        /* aggressor fill */
        {
            order_t fill = *o;
            fill.type = ORD_FILL;
            fill.fill_qty = fq;
            fill.price = b->price;
            if (fiber_send(g_fill_q, &fill, 32)) {
                atomic_fetch_add(&g_orders_filled, 1);
                atomic_fetch_add(&g_fill_qty_total, (unsigned)fq);
            }
        }
        /* resting fill */
        {
            order_t fill;
            memset(&fill, 0, sizeof(fill));
            fill.type = ORD_FILL;
            fill.trader_id = b->trader_id;
            fill.order_id = b->order_id;
            fill.side = b->side;
            fill.price = b->price;
            fill.qty = b->qty_left;
            fill.fill_qty = fq;
            if (fiber_send(g_fill_q, &fill, 32)) {
                atomic_fetch_add(&g_orders_filled, 1);
                atomic_fetch_add(&g_fill_qty_total, (unsigned)fq);
            }
        }

        b->qty_left -= fq;
        remaining -= fq;
        atomic_fetch_add(&g_orders_matched, 1);
        if (b->qty_left <= 0)
            b->active = 0;
    }

    if (remaining > 0) {
        int j;
        for (j = 0; j < bookn; j++) {
            if (!book[j].active) {
                book[j].active = 1;
                book[j].order_id = o->order_id;
                book[j].trader_id = o->trader_id;
                book[j].side = o->side;
                book[j].price = o->price;
                book[j].qty_left = remaining;
                atomic_fetch_add(&g_book_rests, 1);
                return;
            }
        }
        /* book full — reject residual */
        {
            order_t rej = *o;
            rej.type = ORD_REJECT;
            rej.qty = remaining;
            if (fiber_send(g_fill_q, &rej, 16))
                atomic_fetch_add(&g_orders_rejected, 1);
        }
    }
}

static void matcher_fiber(mco_coro* co)
{
    fiber_ctx* cx = (fiber_ctx*)mco_get_user_data(co);
    book_ent book[MAX_BOOK];
    memset(book, 0, sizeof(book));
    unsigned nproc = 0;

    while (!atomic_load(&g_stop) || cchan_size(g_order_q) > 0) {
        order_t o;
        if (!fiber_recv(g_order_q, &o, 16)) {
            if (atomic_load(&g_stop) && cchan_size(g_order_q) == 0)
                break;
            mco_yield(co);
            continue;
        }
        if (o.type == ORD_SHUTDOWN)
            break;
        if (o.type == ORD_NEW) {
            match_one(book, MAX_BOOK, &o);
            nproc++;
        }
        if ((nproc & 7u) == 7u)
            mco_yield(co);
    }

    if (g_verbose)
        INFO("matcher %d exit processed=%u", cx->global_id, nproc);
}

/* -------------------------------------------------------------------------- */
/* fill sink fiber — drains fill_q so matchers never hard-stall               */
/* -------------------------------------------------------------------------- */

static void sink_fiber(mco_coro* co)
{
    (void)co;
    while (!atomic_load(&g_stop) || cchan_size(g_fill_q) > 0) {
        order_t o;
        if (!fiber_recv(g_fill_q, &o, 16)) {
            if (atomic_load(&g_stop) && cchan_size(g_fill_q) == 0)
                break;
            mco_yield(mco_running());
            continue;
        }
        /* counts already done at match time for filled; sink just drains.
           Re-count sinks optionally for integrity: we trust match path. */
        (void)o;
    }
}

/* -------------------------------------------------------------------------- */

static int worker_spawn_local(worker_t* w, void (*fn)(mco_coro*), fiber_ctx* cx)
{
    mco_desc desc;
    mco_coro* co;
    if (w->nlocal >= MAX_LOCAL)
        return -1;
    desc = mco_desc_init(fn, 32 * 1024);
    desc.user_data = cx;
    if (mco_create(&co, &desc) != MCO_SUCCESS)
        return -1;
    w->cos[w->nlocal] = co;
    w->alive[w->nlocal] = 1;
    w->nlocal++;
    return 0;
}

static void* worker_main(void* arg)
{
    worker_t* w = (worker_t*)arg;
    int idle = 0;

    for (;;) {
        int i, any_alive = 0, any_ran = 0;
        for (i = 0; i < w->nlocal; i++) {
            if (!w->alive[i]) continue;
            if (mco_status(w->cos[i]) == MCO_DEAD) {
                w->alive[i] = 0;
                continue;
            }
            any_alive = 1;
            if (mco_status(w->cos[i]) == MCO_SUSPENDED) {
                mco_resume(w->cos[i]);
                any_ran = 1;
                if (mco_status(w->cos[i]) == MCO_DEAD)
                    w->alive[i] = 0;
            }
        }
        if (!any_alive)
            break;
        if (any_ran) {
            idle = 0;
        } else {
            idle++;
            if (atomic_load(&g_stop) && idle > 10000)
                break;
            if ((idle & 127) == 127)
                cchan_sleep(0);
        }
    }

    {
        int j;
        for (j = 0; j < w->nlocal; j++) {
            if (w->cos[j]) {
                mco_destroy(w->cos[j]);
                w->cos[j] = 0;
            }
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* session: run for g_seconds, report every second from main                  */
/* -------------------------------------------------------------------------- */

static void run_sustained(void)
{
    int w, f;
    int nworkers = g_nworkers;
    int nfibers = g_nfibers;
    int ntraders, nmatchers, nsinks;
    pthread_t th[MAX_WORKERS];
    double t_start, t_end;
    unsigned long long prev_sent = 0, prev_matched = 0;
    int sec;

    if (nworkers < 1) nworkers = 1;
    if (nworkers > MAX_WORKERS) nworkers = MAX_WORKERS;
    if (nfibers < 4) nfibers = 4;
    if (nfibers > MAX_FIBERS_TOTAL) nfibers = MAX_FIBERS_TOTAL;

    /*
     * Fiber mix pinned round-robin across workers:
     *   ~70% traders, ~20% matchers, ~10% fill sinks
     */
    ntraders = (nfibers * 7) / 10;
    nmatchers = (nfibers * 2) / 10;
    if (nmatchers < 1) nmatchers = 1;
    nsinks = nfibers - ntraders - nmatchers;
    if (nsinks < 1) {
        nsinks = 1;
        if (ntraders > 2) ntraders--;
    }
    nfibers = ntraders + nmatchers + nsinks;

    HEAD("sustained orders: %d fibers on %d workers for %ds "
         "(%d traders + %d matchers + %d sinks)",
         nfibers, nworkers, g_seconds, ntraders, nmatchers, nsinks);

    atomic_store(&g_stop, 0);
    atomic_store(&g_next_oid, 1);
    atomic_store(&g_orders_sent, 0);
    atomic_store(&g_orders_matched, 0);
    atomic_store(&g_orders_filled, 0);
    atomic_store(&g_orders_rejected, 0);
    atomic_store(&g_fill_qty_total, 0);
    atomic_store(&g_book_rests, 0);

    g_order_q = cchan_create(8192, (unsigned short)sizeof(order_t));
    g_fill_q  = cchan_create(8192, (unsigned short)sizeof(order_t));

    memset(g_workers, 0, sizeof(g_workers));
    for (w = 0; w < nworkers; w++)
        g_workers[w].worker_id = w;

    for (f = 0; f < nfibers; f++) {
        fiber_ctx* cx = &g_fctx[f];
        int wid = f % nworkers;
        worker_t* wr = &g_workers[wid];
        void (*fn)(mco_coro*);
        cx->global_id = f;
        cx->worker_id = wid;
        if (f < ntraders) {
            cx->is_trader = 1;
            fn = trader_fiber;
        } else if (f < ntraders + nmatchers) {
            cx->is_trader = 0;
            fn = matcher_fiber;
        } else {
            cx->is_trader = 0;
            fn = sink_fiber;
        }
        if (worker_spawn_local(wr, fn, cx) != 0)
            FAIL("spawn fiber %d", f);
    }

    {
        int total = 0;
        for (w = 0; w < nworkers; w++) {
            total += g_workers[w].nlocal;
            INFO("worker M%d hosts %d fibers", w, g_workers[w].nlocal);
        }
        INFO("pinned %d fibers", total);
    }

    for (w = 0; w < nworkers; w++) {
        if (pthread_create(&th[w], 0, worker_main, &g_workers[w]) != 0)
            FAIL("pthread_create worker %d", w);
    }

    t_start = now_s();
    printf("\n  %4s  %12s  %12s  %12s  %10s  %10s  %8s  %8s\n",
           "sec", "orders", "matches", "fills", "ord/s", "match/s", "q_ord", "q_fill");
    printf("  %4s  %12s  %12s  %12s  %10s  %10s  %8s  %8s\n",
           "----", "------------", "------------", "------------",
           "----------", "----------", "--------", "--------");
    fflush(stdout);

    for (sec = 1; sec <= g_seconds; sec++) {
        unsigned long long sent, matched, filled, rejected, fqty, rests;
        unsigned long long d_sent, d_matched;
        int qord, qfill;
        double elapsed;

        cchan_sleep(1000);

        sent = atomic_load(&g_orders_sent);
        matched = atomic_load(&g_orders_matched);
        filled = atomic_load(&g_orders_filled);
        rejected = atomic_load(&g_orders_rejected);
        fqty = atomic_load(&g_fill_qty_total);
        rests = atomic_load(&g_book_rests);
        qord = cchan_size(g_order_q);
        qfill = cchan_size(g_fill_q);
        d_sent = sent - prev_sent;
        d_matched = matched - prev_matched;
        elapsed = now_s() - t_start;

        printf("  %4d  %12llu  %12llu  %12llu  %10.0f  %10.0f  %8d  %8d\n",
               sec,
               (unsigned long long)sent,
               (unsigned long long)matched,
               (unsigned long long)filled,
               (double)d_sent,
               (double)d_matched,
               qord, qfill);
        fflush(stdout);

        prev_sent = sent;
        prev_matched = matched;
        (void)rejected;
        (void)fqty;
        (void)rests;
        (void)elapsed;
    }

    t_end = now_s();
    INFO("duration complete (%.2fs) — signaling stop", t_end - t_start);
    atomic_store(&g_stop, 1);

    /* wake anyone sitting on full/empty queues */
    cchan_close(g_order_q);
    cchan_close(g_fill_q);

    for (w = 0; w < nworkers; w++)
        pthread_join(th[w], 0);

    {
        unsigned long long sent = atomic_load(&g_orders_sent);
        unsigned long long matched = atomic_load(&g_orders_matched);
        unsigned long long filled = atomic_load(&g_orders_filled);
        unsigned long long rejected = atomic_load(&g_orders_rejected);
        unsigned long long fqty = atomic_load(&g_fill_qty_total);
        double wall = t_end - t_start;
        INFO("FINAL orders_sent=%llu  matches=%llu  fill_msgs=%llu  rejects=%llu  fill_qty=%llu",
             sent, matched, filled, rejected, fqty);
        INFO("FINAL avg order rate=%.0f/s  match rate=%.0f/s  over %.1fs",
             sent / (wall + 1e-9), matched / (wall + 1e-9), wall);
        INFO("model: %d OS threads × ~%d fibers/thread",
             nworkers, (nfibers + nworkers - 1) / nworkers);

        if (sent == 0)
            FAIL("no orders processed");
        if (matched == 0 && sent > 1000)
            FAIL("no matches despite many orders");
    }

    cchan_dispose(g_order_q);
    cchan_dispose(g_fill_q);
}

/* -------------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    int i;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;

    g_nworkers = (int)(ncpu > 8 ? 8 : ncpu);
    g_nfibers = 500;
    g_seconds = 60;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) {
            g_quick = 1;
            g_seconds = 5;
            g_nfibers = 64;
            g_nworkers = g_nworkers > 4 ? 4 : g_nworkers;
        } else if (strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (strncmp(argv[i], "--fibers=", 9) == 0) {
            g_nfibers = atoi(argv[i] + 9);
        } else if (strncmp(argv[i], "--workers=", 10) == 0) {
            g_nworkers = atoi(argv[i] + 10);
        } else if (strncmp(argv[i], "--seconds=", 10) == 0) {
            g_seconds = atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                "usage: %s [--quick] [-v] [--fibers=N] [--workers=M] [--seconds=S]\n"
                "  default: 500 fibers, workers=min(8,ncpu), 60 seconds, stats/1s\n",
                argv[0]);
            return 0;
        }
    }
    if (g_seconds < 1) g_seconds = 1;

    printf("cchan + minicoro SUSTAINED ORDER PROCESSING\n");
    INFO("host_cpus=%ld  workers=%d  fibers=%d  duration=%ds",
         ncpu, g_nworkers, g_nfibers, g_seconds);
    INFO("main thread reports throughput every 1 second");

    run_sustained();

    printf("\n========================================\n");
    if (g_fail == 0) {
        printf("FIBER WORKERS: ALL PASSED\n");
        return 0;
    }
    printf("FIBER WORKERS: %d FAILURES\n", g_fail);
    return 1;
}
