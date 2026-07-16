/* classy-cchan-fibers.cy — cooperative fibers + cchan (minicoro)
 *
 * Port of the scheduling pattern from ext/ccchan/fiber_workers.c:
 *
 *   trader fibers ──orders──► matcher fibers ──fills──► sink fibers
 *        pinned across M OS worker threads (never resume a fiber on another M)
 *        cross-thread messages ride on buffered cchan queues
 *
 * Fiber rules (critical — same as fiber_workers):
 *   - Use cchan_try_* + mco_yield — never blocking cchan_send/recv on a fiber
 *     that shares an OS thread with its peer (that deadlocks the local runqueue).
 *   - Prefer buffered channels so try_send has somewhere to park messages.
 *   - stop → close → join (never join-then-stop).
 *
 * Shared counters / stop flag use C11 atomics (`<stdatomic.h>` → MIR ALOAD /
 * ASTORE / AADD). Channel traffic still uses cchan’s own pthread mutexes.
 *
 * Run from the project root (all driver options before -eg; program args after):
 *
 *   ./bin/classyc -I ext/ccchan -w examples/classy-cchan-fibers.cy -eg
 *   ./bin/classyc -I ext/ccchan -w examples/classy-cchan-fibers.cy -eg --quick
 *   ./bin/classyc -I ext/ccchan -w examples/classy-cchan-fibers.cy -eg \
 *       --workers=4 --fibers=64 --seconds=2
 *
 * ClassyC does not implement real `_Thread_local` storage (it warns and shares
 * one cell across pthreads). minicoro needs true per-thread `mco_current_co`,
 * so this example enables MCO_PTHREAD_TLS (pthread_key) before including
 * minicoro. Without that, --workers>1 corrupts resume state and asserts.
 * Pthreads resolve from the host `classyc` process — no -l pthread needed.
 *
 * @expect: exit 0 and print "FIBER CHANNEL SMOKE PASSED"
 */

/* Real TLS via pthread_key — required under ClassyC for multi-OS-thread fibers */
#define MCO_PTHREAD_TLS
#define MINICORO_IMPL
#include "minicoro.h"

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

/* ── knobs (small defaults so the example finishes in ~1s) ────────────── */

#define MAX_WORKERS      8
#define MAX_LOCAL        64
#define MAX_FIBERS_TOTAL 256
#define MAX_BOOK         32
#define FIBER_STACK      (64 * 1024)

static int g_nworkers = 2;    /* multi-OS-thread; needs MCO_PTHREAD_TLS under ClassyC */
static int g_nfibers  = 24;   /* total fibers (traders + matchers + sinks) */
static int g_seconds  = 1;    /* sustained run length */

/* ── shared counters (stdatomic → MIR seq_cst; same as fiber_workers.c) ─ */

static atomic_int g_stop;
static atomic_int g_next_oid;

static atomic_ullong g_orders_sent;
static atomic_ullong g_orders_matched;
static atomic_ullong g_orders_filled;
static atomic_ullong g_orders_rejected;
static atomic_ullong g_fill_qty_total;
static atomic_ullong g_book_rests;

/* ── fiber-friendly channel I/O (try + yield) ─────────────────────────── */

static int fiber_send(cchan_t *c, const void *msg, int max_yields) {
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_send(c, msg);
        if (rc == 1) return 1;
        if (rc < 0) return 0; /* closed */
        if (y < max_yields) {
            mco_coro *self = mco_running();
            if (!self) return 0;
            mco_yield(self);
        }
    }
    return 0;
}

static int fiber_recv(cchan_t *c, void *msg, int max_yields) {
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_recv(c, msg);
        if (rc == 1) return 1;
        if (rc < 0) return 0; /* closed empty */
        if (y < max_yields) {
            mco_coro *self = mco_running();
            if (!self) return 0;
            mco_yield(self);
        }
    }
    return 0;
}

/* ── order protocol ───────────────────────────────────────────────────── */

enum {
    ORD_NEW = 1,
    ORD_FILL,
    ORD_REJECT,
    ORD_SHUTDOWN
};

typedef struct {
    int32_t type;
    int32_t trader_id;
    int32_t order_id;
    int32_t side;      /* 0 buy, 1 sell */
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

static cchan_t *g_order_q;  /* traders → matchers */
static cchan_t *g_fill_q;   /* matchers → sinks   */

/* ── worker + fiber tables ────────────────────────────────────────────── */

typedef struct {
    int       worker_id;
    int       nlocal;
    mco_coro *cos[MAX_LOCAL];
    int       alive[MAX_LOCAL];
} worker_t;

static worker_t g_workers[MAX_WORKERS];

typedef struct {
    int global_id;
    int worker_id;
    int role; /* 0 trader, 1 matcher, 2 sink */
} fiber_ctx;

static fiber_ctx g_fctx[MAX_FIBERS_TOTAL];

/* ── trader fiber ─────────────────────────────────────────────────────── */

static void trader_fiber(mco_coro *co) {
    fiber_ctx *cx = (fiber_ctx *)mco_get_user_data(co);
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
            if (atomic_load(&g_stop)) break;
            mco_yield(co);
            continue;
        }
        atomic_fetch_add(&g_orders_sent, 1);
        seq++;
        burst++;
        if ((burst & 3u) == 3u)
            mco_yield(co);
    }
}

/* ── matcher fiber ────────────────────────────────────────────────────── */

static void match_one(book_ent *book, int bookn, order_t *o) {
    int i;
    int remaining = o->qty;

    for (i = 0; i < bookn && remaining > 0; i++) {
        book_ent *b = &book[i];
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
                atomic_fetch_add(&g_fill_qty_total, (unsigned long long)fq);
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
                atomic_fetch_add(&g_fill_qty_total, (unsigned long long)fq);
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

static void matcher_fiber(mco_coro *co) {
    fiber_ctx *cx = (fiber_ctx *)mco_get_user_data(co);
    book_ent book[MAX_BOOK];
    unsigned nproc = 0;
    (void)cx;

    memset(book, 0, sizeof(book));
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
}

/* ── fill sink — drains fill_q so matchers never hard-stall ───────────── */

static void sink_fiber(mco_coro *co) {
    (void)co;
    while (!atomic_load(&g_stop) || cchan_size(g_fill_q) > 0) {
        order_t o;
        if (!fiber_recv(g_fill_q, &o, 16)) {
            if (atomic_load(&g_stop) && cchan_size(g_fill_q) == 0)
                break;
            mco_yield(mco_running());
            continue;
        }
        (void)o; /* counts updated at match time */
    }
}

/* ── per-OS-thread runqueue ───────────────────────────────────────────── */

static int worker_spawn_local(worker_t *w, void (*fn)(mco_coro *), fiber_ctx *cx) {
    mco_desc desc;
    mco_coro *co;
    if (w->nlocal >= MAX_LOCAL)
        return -1;
    desc = mco_desc_init(fn, FIBER_STACK);
    desc.user_data = cx;
    if (mco_create(&co, &desc) != MCO_SUCCESS)
        return -1;
    w->cos[w->nlocal] = co;
    w->alive[w->nlocal] = 1;
    w->nlocal++;
    return 0;
}

static void *worker_main(void *arg) {
    worker_t *w = (worker_t *)arg;
    int idle = 0;

    for (;;) {
        int i;
        int any_alive = 0;
        int any_ran = 0;

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

/* ── session ──────────────────────────────────────────────────────────── */

static void run_session(void) {
    int w, f;
    int nworkers = g_nworkers;
    int nfibers = g_nfibers;
    int ntraders, nmatchers, nsinks;
    pthread_t th[MAX_WORKERS];
    int sec;
    unsigned long long prev_sent = 0, prev_matched = 0;

    if (nworkers < 1) nworkers = 1;
    if (nworkers > MAX_WORKERS) nworkers = MAX_WORKERS;
    if (nfibers < 4) nfibers = 4;
    if (nfibers > MAX_FIBERS_TOTAL) nfibers = MAX_FIBERS_TOTAL;

    /* ~70% traders, ~20% matchers, ~10% sinks (same mix as fiber_workers) */
    ntraders = (nfibers * 7) / 10;
    nmatchers = (nfibers * 2) / 10;
    if (nmatchers < 1) nmatchers = 1;
    nsinks = nfibers - ntraders - nmatchers;
    if (nsinks < 1) {
        nsinks = 1;
        if (ntraders > 2) ntraders--;
    }
    nfibers = ntraders + nmatchers + nsinks;

    printf("\n=== fiber orders: %d fibers on %d workers for %ds "
           "(%d traders + %d matchers + %d sinks) ===\n",
           nfibers, nworkers, g_seconds, ntraders, nmatchers, nsinks);

    atomic_store(&g_stop, 0);
    atomic_store(&g_next_oid, 1);
    atomic_store(&g_orders_sent, 0);
    atomic_store(&g_orders_matched, 0);
    atomic_store(&g_orders_filled, 0);
    atomic_store(&g_orders_rejected, 0);
    atomic_store(&g_fill_qty_total, 0);
    atomic_store(&g_book_rests, 0);

    g_order_q = cchan_create(2048, (unsigned short)sizeof(order_t));
    g_fill_q  = cchan_create(2048, (unsigned short)sizeof(order_t));
    if (!g_order_q || !g_fill_q) {
        printf("FAIL: cchan_create\n");
        return;
    }

    memset(g_workers, 0, sizeof(g_workers));
    for (w = 0; w < nworkers; w++)
        g_workers[w].worker_id = w;

    for (f = 0; f < nfibers; f++) {
        fiber_ctx *cx = &g_fctx[f];
        int wid = f % nworkers;
        worker_t *wr = &g_workers[wid];
        void (*fn)(mco_coro *);

        cx->global_id = f;
        cx->worker_id = wid;
        if (f < ntraders) {
            cx->role = 0;
            fn = trader_fiber;
        } else if (f < ntraders + nmatchers) {
            cx->role = 1;
            fn = matcher_fiber;
        } else {
            cx->role = 2;
            fn = sink_fiber;
        }
        if (worker_spawn_local(wr, fn, cx) != 0) {
            printf("FAIL: spawn fiber %d\n", f);
            return;
        }
    }

    {
        int total = 0;
        for (w = 0; w < nworkers; w++) {
            total += g_workers[w].nlocal;
            printf("  worker M%d hosts %d fibers\n", w, g_workers[w].nlocal);
        }
        printf("  pinned %d fibers\n", total);
    }

    for (w = 0; w < nworkers; w++) {
        if (pthread_create(&th[w], 0, worker_main, &g_workers[w]) != 0) {
            printf("FAIL: pthread_create worker %d\n", w);
            return;
        }
    }

    printf("\n  %4s  %12s  %12s  %12s  %10s  %10s  %8s  %8s\n",
           "sec", "orders", "matches", "fills", "ord/s", "match/s", "q_ord", "q_fill");
    printf("  %4s  %12s  %12s  %12s  %10s  %10s  %8s  %8s\n",
           "----", "------------", "------------", "------------",
           "----------", "----------", "--------", "--------");

    for (sec = 1; sec <= g_seconds; sec++) {
        unsigned long long sent, matched, filled;
        unsigned long long dsent, dmatched;

        cchan_sleep(1000);

        sent = atomic_load(&g_orders_sent);
        matched = atomic_load(&g_orders_matched);
        filled = atomic_load(&g_orders_filled);
        dsent = sent - prev_sent;
        dmatched = matched - prev_matched;
        prev_sent = sent;
        prev_matched = matched;

        printf("  %4d  %12llu  %12llu  %12llu  %10llu  %10llu  %8d  %8d\n",
               sec, sent, matched, filled, dsent, dmatched,
               cchan_size(g_order_q), cchan_size(g_fill_q));
        fflush(stdout);
    }

    /* stop → close → join */
    atomic_store(&g_stop, 1);
    cchan_close(g_order_q);
    cchan_close(g_fill_q);

    for (w = 0; w < nworkers; w++)
        pthread_join(th[w], 0);

    printf("\n  final: sent=%llu matched=%llu filled=%llu rejected=%llu "
           "fill_qty=%llu rests=%llu\n",
           (unsigned long long)atomic_load(&g_orders_sent),
           (unsigned long long)atomic_load(&g_orders_matched),
           (unsigned long long)atomic_load(&g_orders_filled),
           (unsigned long long)atomic_load(&g_orders_rejected),
           (unsigned long long)atomic_load(&g_fill_qty_total),
           (unsigned long long)atomic_load(&g_book_rests));

    cchan_dispose(g_order_q);
    cchan_dispose(g_fill_q);
    g_order_q = 0;
    g_fill_q = 0;
}

int main(int argc, char **argv) {
    int i;

    /* optional overrides: --workers=N --fibers=N --seconds=N  or  --quick */
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "--quick") == 0) {
            g_nworkers = 2;
            g_nfibers = 16;
            g_seconds = 1;
        } else if (strncmp(a, "--workers=", 10) == 0) {
            g_nworkers = atoi(a + 10);
        } else if (strncmp(a, "--fibers=", 9) == 0) {
            g_nfibers = atoi(a + 9);
        } else if (strncmp(a, "--seconds=", 10) == 0) {
            g_seconds = atoi(a + 10);
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            printf("usage: ./bin/classyc -I ext/ccchan -w "
                   "examples/classy-cchan-fibers.cy -eg"
                   " [--quick] [--workers=N] [--fibers=N] [--seconds=N]\n");
            return 0;
        }
    }

    printf("classy-cchan-fibers: minicoro fibers + cchan (fiber_workers pattern)\n");
    printf("  defaults: %d workers, %d fibers, %ds\n",
           g_nworkers, g_nfibers, g_seconds);

    run_session();

    if (atomic_load(&g_orders_sent) == 0) {
        printf("\nFAIL: no orders sent (scheduler or channel stuck?)\n");
        return 1;
    }
    if (atomic_load(&g_orders_matched) == 0 && atomic_load(&g_book_rests) == 0) {
        printf("\nFAIL: no matches and no book rests\n");
        return 1;
    }

    printf("\nFIBER CHANNEL SMOKE PASSED\n");
    return 0;
}
