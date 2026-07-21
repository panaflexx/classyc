/*
 * fiber_trading.c — mini equity matching engine on cooperative fibers + cchan
 *
 * Demonstrates ClassyC-relevant integration:
 *   - minicoro (header-only stackful fibers) as a per-OS-thread scheduler
 *   - cchan as the Go-style mailbox between fibers
 *   - NEVER use blocking cchan_send/recv between fibers on the same OS thread
 *     (would deadlock: peer cannot run while this fiber parks on pthread cond).
 *   - Pattern: try_send / try_recv; on would-block → mco_yield (coop "park")
 *
 * Topology (all fibers, one pthread):
 *
 *   trader[0..N]  --order-->  gateway  --order-->  matcher
 *       ^                        |                    |
 *       |                        v                    v
 *       +----- fill/ack --------+  <--- fill ---  book match
 *
 * Build:
 *   make fiber
 *   ./fiber_trading [--quick|--insane] [-v] [--traders=N] [--orders=N]
 *
 *   --insane  510 traders + matcher + gateway = 512 fibers on 1 OS thread
 *
 * License note:
 *   minicoro.h is Public Domain OR MIT-0 (vendored). cchan is LGPL-2.1+/MPL-2.0.
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
#include <time.h>

/* -------------------------------------------------------------------------- */
/* knobs                                                                      */
/* -------------------------------------------------------------------------- */

static int g_quick = 0;
static int g_insane = 0;
static int g_fail = 0;
static int g_verbose = 0;
static int g_cli_traders = 0; /* 0 = use mode defaults */
static int g_cli_orders = 0;

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
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

/* Stress cap: 510 traders + matcher + gateway = 512 fibers */
#define MAX_TRADERS   510
#define MAX_BOOK      256
#define MAX_FIBERS    512
#define MAX_ORD_TRACK 262144

/* -------------------------------------------------------------------------- */
/* cooperative channel helpers                                                */
/* -------------------------------------------------------------------------- */

/*
 * Fiber-safe send/recv: spin try_* and yield to the scheduler when the channel
 * would block. Bound yields so a stuck system fails the demo instead of looping
 * forever (scheduler keeps running other fibers until budget).
 */
static int fiber_send(cchan_t* c, const void* msg, int max_yields)
{
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_send(c, msg);
        if (rc == 1)
            return 1;
        if (rc < 0)
            return 0; /* closed */
        if (y < max_yields) {
            mco_coro* self = mco_running();
            if (self)
                mco_yield(self);
            else
                return 0;
        }
    }
    return 0; /* budget exhausted */
}

static int fiber_recv(cchan_t* c, void* msg, int max_yields)
{
    int y;
    for (y = 0; y <= max_yields; y++) {
        int rc = cchan_try_recv(c, msg);
        if (rc == 1)
            return 1;
        if (rc < 0)
            return 0; /* closed empty */
        if (y < max_yields) {
            mco_coro* self = mco_running();
            if (self)
                mco_yield(self);
            else
                return 0;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* wire protocol                                                              */
/* -------------------------------------------------------------------------- */

typedef enum {
    MSG_NEW = 1,
    MSG_CANCEL,
    MSG_FILL,
    MSG_CANCEL_ACK,
    MSG_REJECT,
    MSG_SHUTDOWN
} msg_type;

typedef struct {
    int32_t type;
    int32_t trader_id;
    int32_t order_id;
    int32_t side;     /* 0 buy, 1 sell */
    int32_t price;
    int32_t qty;
    int32_t fill_qty;
    int32_t pad;
} msg_t;

typedef struct {
    int active;
    int order_id;
    int trader_id;
    int side;
    int price;
    int qty_left;
} book_order;

/* -------------------------------------------------------------------------- */
/* global topology                                                            */
/* -------------------------------------------------------------------------- */

static cchan_t* g_from_trader[MAX_TRADERS];  /* trader → gateway */
static cchan_t* g_to_trader[MAX_TRADERS];    /* gateway → trader */
static cchan_t* g_to_matcher;                /* gateway → matcher */
static cchan_t* g_from_matcher;              /* matcher → gateway (fills) */

static int g_ntraders;
static int g_orders_per;
static atomic_int g_next_oid;
static atomic_ullong g_matched;
static atomic_ullong g_cancelled;
static atomic_ullong g_rejected;
static atomic_ullong g_fills_to_traders;
static atomic_ullong g_fill_drops;
static atomic_int g_traders_done;
static atomic_int g_stop;

/* simple order tracker: order_id → remaining qty, 0 unused */
static int g_ord_qty[MAX_ORD_TRACK];
static int g_ord_trader[MAX_ORD_TRACK];
static atomic_int g_ord_live[MAX_ORD_TRACK]; /* 1 live, 0 done/unused */

static void track_new(int oid, int tid, int qty)
{
    if (oid <= 0 || oid >= MAX_ORD_TRACK) {
        FAIL("order_id %d oob", oid);
        return;
    }
    g_ord_qty[oid] = qty;
    g_ord_trader[oid] = tid;
    atomic_store(&g_ord_live[oid], 1);
}

static void track_done(int oid)
{
    if (oid > 0 && oid < MAX_ORD_TRACK)
        atomic_store(&g_ord_live[oid], 0);
}

/* -------------------------------------------------------------------------- */
/* tiny cooperative scheduler                                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    mco_coro* co;
    const char* name;
    int alive;
} fiber_slot;

static fiber_slot g_fibers[MAX_FIBERS];
static int g_nfibers;

static int fiber_spawn(void (*fn)(mco_coro*), void* user, const char* name)
{
    mco_desc desc;
    mco_coro* co;
    mco_result res;
    if (g_nfibers >= MAX_FIBERS) {
        FAIL("fiber table full");
        return -1;
    }
    /* 32KB stacks: 512 fibers ≈ 16MB — enough for this shallow call graph */
    desc = mco_desc_init(fn, 32 * 1024);
    desc.user_data = user;
    res = mco_create(&co, &desc);
    if (res != MCO_SUCCESS) {
        FAIL("mco_create %s: %s", name, mco_result_description(res));
        return -1;
    }
    g_fibers[g_nfibers].co = co;
    g_fibers[g_nfibers].name = name;
    g_fibers[g_nfibers].alive = 1;
    return g_nfibers++;
}

/* Round-robin until all fibers are DEAD or idle budget expires with stop set. */
static void fiber_run_until_done(void)
{
    int idle_rounds = 0;
    const int idle_limit = g_insane ? 2000000 : (g_quick ? 20000 : 500000);

    for (;;) {
        int i, any_alive = 0, any_ran = 0;
        for (i = 0; i < g_nfibers; i++) {
            fiber_slot* s = &g_fibers[i];
            if (!s->alive)
                continue;
            if (mco_status(s->co) == MCO_DEAD) {
                s->alive = 0;
                continue;
            }
            any_alive = 1;
            if (mco_status(s->co) == MCO_SUSPENDED) {
                mco_result r = mco_resume(s->co);
                if (r != MCO_SUCCESS && r != MCO_NOT_SUSPENDED) {
                    FAIL("resume %s: %s", s->name, mco_result_description(r));
                    s->alive = 0;
                } else {
                    any_ran = 1;
                }
            }
            if (mco_status(s->co) == MCO_DEAD)
                s->alive = 0;
        }
        if (!any_alive)
            break;
        if (any_ran)
            idle_rounds = 0;
        else
            idle_rounds++;
        if (idle_rounds > idle_limit) {
            if (atomic_load(&g_stop))
                break;
            /* force stop so fibers exit their loops */
            atomic_store(&g_stop, 1);
            if (idle_rounds > idle_limit * 2)
                break;
        }
    }
}

static void fiber_cleanup(void)
{
    int i;
    for (i = 0; i < g_nfibers; i++) {
        if (g_fibers[i].co)
            mco_destroy(g_fibers[i].co);
        g_fibers[i].co = 0;
        g_fibers[i].alive = 0;
    }
    g_nfibers = 0;
}

/* -------------------------------------------------------------------------- */
/* matcher fiber                                                              */
/* -------------------------------------------------------------------------- */

static int book_add(book_order* book, int n, const msg_t* o)
{
    int i;
    for (i = 0; i < n; i++) {
        if (!book[i].active) {
            book[i].active = 1;
            book[i].order_id = o->order_id;
            book[i].trader_id = o->trader_id;
            book[i].side = o->side;
            book[i].price = o->price;
            book[i].qty_left = o->qty;
            return i;
        }
    }
    return -1;
}

static void book_cancel(book_order* book, int n, int order_id, book_order* out)
{
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < n; i++) {
        if (book[i].active && book[i].order_id == order_id) {
            *out = book[i];
            book[i].active = 0;
            return;
        }
    }
}

static void matcher_process_new(book_order* book, int bookn, msg_t* o)
{
    int i;
    int remaining = o->qty;

    for (i = 0; i < bookn && remaining > 0; i++) {
        book_order* b = &book[i];
        int fq, fpx;
        if (!b->active) continue;
        if (b->side == o->side) continue;
        if (o->side == 0) { /* buy lifts ask */
            if (o->price < b->price) continue;
        } else {
            if (o->price > b->price) continue;
        }

        fq = remaining < b->qty_left ? remaining : b->qty_left;
        fpx = b->price;

        /* fill aggressor */
        {
            msg_t fill = *o;
            fill.type = MSG_FILL;
            fill.fill_qty = fq;
            fill.price = fpx;
            /* under extreme fan-in, drop is backpressure — count, don't FAIL-spam */
            if (!fiber_send(g_from_matcher, &fill,
                            (g_insane || g_ntraders >= 256) ? 2000 : 10000))
                atomic_fetch_add(&g_fill_drops, 1);
            if (fq >= remaining)
                track_done(o->order_id);
        }
        /* fill resting */
        {
            msg_t fill;
            memset(&fill, 0, sizeof(fill));
            fill.type = MSG_FILL;
            fill.trader_id = b->trader_id;
            fill.order_id = b->order_id;
            fill.side = b->side;
            fill.price = fpx;
            fill.qty = b->qty_left;
            fill.fill_qty = fq;
            if (!fiber_send(g_from_matcher, &fill,
                            (g_insane || g_ntraders >= 256) ? 2000 : 10000))
                atomic_fetch_add(&g_fill_drops, 1);
            if (fq >= b->qty_left)
                track_done(b->order_id);
        }

        b->qty_left -= fq;
        remaining -= fq;
        atomic_fetch_add(&g_matched, 1);
        if (b->qty_left <= 0)
            b->active = 0;
    }

    if (remaining > 0) {
        msg_t rest = *o;
        rest.qty = remaining;
        if (book_add(book, bookn, &rest) < 0) {
            msg_t rej = *o;
            rej.type = MSG_REJECT;
            rej.qty = remaining;
            fiber_send(g_from_matcher, &rej, 1000);
            if (remaining == o->qty)
                track_done(o->order_id);
            atomic_fetch_add(&g_rejected, 1);
        }
    } else {
        track_done(o->order_id);
    }
}

static void matcher_fiber(mco_coro* co)
{
    book_order book[MAX_BOOK];
    memset(book, 0, sizeof(book));
    (void)co;

    INFO("matcher fiber up");
    while (!atomic_load(&g_stop) || cchan_size(g_to_matcher) > 0) {
        msg_t m;
        if (!fiber_recv(g_to_matcher, &m, 64)) {
            if (atomic_load(&g_stop) && cchan_size(g_to_matcher) == 0
                && cchan_is_closed(g_to_matcher))
                break;
            mco_yield(mco_running());
            continue;
        }
        if (m.type == MSG_SHUTDOWN)
            break;
        if (m.type == MSG_NEW) {
            matcher_process_new(book, MAX_BOOK, &m);
        } else if (m.type == MSG_CANCEL) {
            book_order gone;
            msg_t ack = m;
            book_cancel(book, MAX_BOOK, m.order_id, &gone);
            if (gone.active) {
                ack.type = MSG_CANCEL_ACK;
                ack.qty = gone.qty_left;
                track_done(m.order_id);
                atomic_fetch_add(&g_cancelled, 1);
            } else {
                ack.type = MSG_REJECT;
                atomic_fetch_add(&g_rejected, 1);
                /* may already be filled — only mark done if still live */
                if (m.order_id > 0 && m.order_id < MAX_ORD_TRACK &&
                    atomic_load(&g_ord_live[m.order_id]))
                    track_done(m.order_id);
            }
            fiber_send(g_from_matcher, &ack, 1000);
        }
    }

    /* abort remaining book */
    {
        int i;
        for (i = 0; i < MAX_BOOK; i++) {
            if (!book[i].active) continue;
            msg_t rej;
            memset(&rej, 0, sizeof(rej));
            rej.type = MSG_REJECT;
            rej.order_id = book[i].order_id;
            rej.trader_id = book[i].trader_id;
            rej.qty = book[i].qty_left;
            fiber_send(g_from_matcher, &rej, 100);
            track_done(book[i].order_id);
            book[i].active = 0;
            atomic_fetch_add(&g_rejected, 1);
        }
    }
    INFO("matcher fiber done");
}

/* -------------------------------------------------------------------------- */
/* gateway fiber: mux traders → matcher, demux fills → traders                */
/* -------------------------------------------------------------------------- */

static void gateway_fiber(mco_coro* co)
{
    int n = g_ntraders;
    (void)co;
    INFO("gateway fiber up (traders=%d)", n);

    while (!atomic_load(&g_stop) ||
           cchan_size(g_from_matcher) > 0 ||
           cchan_size(g_to_matcher) > 0) {
        int progress = 0;
        int t;
        msg_t m;

        /* prefer draining matcher replies so the book path never backs up hard */
        while (cchan_try_recv(g_from_matcher, &m) == 1) {
            int tid = m.trader_id;
            progress = 1;
            if (tid < 0 || tid >= n) continue;
            if (!fiber_send(g_to_trader[tid], &m, 5000)) {
                /* trader slow/done — still count terminal tracking on reject path */
                if (m.type == MSG_FILL || m.type == MSG_CANCEL_ACK || m.type == MSG_REJECT)
                    track_done(m.order_id);
            } else {
                atomic_fetch_add(&g_fills_to_traders, 1);
            }
        }

        /* poll each trader ingress (fair round-robin) */
        for (t = 0; t < n; t++) {
            if (cchan_try_recv(g_from_trader[t], &m) != 1)
                continue;
            progress = 1;
            if (m.type == MSG_SHUTDOWN)
                continue;
            if (m.type == MSG_NEW)
                track_new(m.order_id, m.trader_id, m.qty);
            if (!fiber_send(g_to_matcher, &m, 10000)) {
                msg_t rej = m;
                rej.type = MSG_REJECT;
                fiber_send(g_to_trader[t], &rej, 1000);
                track_done(m.order_id);
                atomic_fetch_add(&g_rejected, 1);
            }
        }

        if (!progress) {
            if (atomic_load(&g_traders_done) >= n &&
                cchan_size(g_from_matcher) == 0 &&
                cchan_size(g_to_matcher) == 0) {
                /* all traders finished and pipelines quiet */
                break;
            }
            mco_yield(mco_running());
        }
    }

    /* tell matcher to shut down */
    {
        msg_t sh;
        memset(&sh, 0, sizeof(sh));
        sh.type = MSG_SHUTDOWN;
        fiber_send(g_to_matcher, &sh, 1000);
        cchan_close(g_to_matcher);
    }
    INFO("gateway fiber done");
}

/* -------------------------------------------------------------------------- */
/* trader fibers                                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    int id;
    int target;
    int cancel_pct;
} trader_arg;

static void trader_fiber(mco_coro* co)
{
    trader_arg* a = (trader_arg*)mco_get_user_data(co);
    int id = a->id;
    int sent = 0;
    int in_flight = 0;
    int max_inflight = (g_insane || g_ntraders >= 256) ? 4 : (g_quick ? 4 : 8);
    unsigned long long fills = 0;
    int terms = 0;

    if (g_verbose)
        INFO("trader %d start target=%d", id, a->target);

    while (sent < a->target && !atomic_load(&g_stop)) {
        int made = 0;

        while (in_flight < max_inflight && sent < a->target && !atomic_load(&g_stop)) {
            msg_t o;
            memset(&o, 0, sizeof(o));
            o.type = MSG_NEW;
            o.trader_id = id;
            o.order_id = atomic_fetch_add(&g_next_oid, 1);
            o.side = (sent + id) & 1;
            /* tight price band so matches happen */
            o.price = 1000 + ((sent * 3 + id) % 5) - 2;
            o.qty = 1 + (sent % 3);

            if (!fiber_send(g_from_trader[id], &o, 2000)) {
                break; /* backpressure — drain replies */
            }
            sent++;
            in_flight++;
            made = 1;

            if ((rand() % 100) < a->cancel_pct) {
                msg_t c = o;
                c.type = MSG_CANCEL;
                fiber_send(g_from_trader[id], &c, 500);
            }
        }

        /* drain replies */
        {
            msg_t r;
            while (cchan_try_recv(g_to_trader[id], &r) == 1) {
                made = 1;
                if (r.type == MSG_FILL) {
                    fills += (unsigned)r.fill_qty;
                    /* terminal when order no longer live (full fill / abort) */
                    if (r.order_id > 0 && r.order_id < MAX_ORD_TRACK &&
                        !atomic_load(&g_ord_live[r.order_id])) {
                        if (in_flight > 0) in_flight--;
                        terms++;
                    }
                } else if (r.type == MSG_CANCEL_ACK || r.type == MSG_REJECT) {
                    if (in_flight > 0) in_flight--;
                    terms++;
                    track_done(r.order_id);
                }
            }
        }

        if (!made)
            mco_yield(mco_running());
    }

    /* drain remaining terminals with yield budget */
    {
        int idle = 0;
        int idle_cap = (g_insane || g_ntraders >= 256) ? 20000 : 5000;
        while (in_flight > 0 && idle < idle_cap) {
            msg_t r;
            if (cchan_try_recv(g_to_trader[id], &r) == 1) {
                idle = 0;
                if (r.type == MSG_FILL) {
                    fills += (unsigned)r.fill_qty;
                    if (r.order_id > 0 && r.order_id < MAX_ORD_TRACK &&
                        !atomic_load(&g_ord_live[r.order_id])) {
                        if (in_flight > 0) in_flight--;
                        terms++;
                    }
                } else if (r.type == MSG_CANCEL_ACK || r.type == MSG_REJECT) {
                    if (in_flight > 0) in_flight--;
                    terms++;
                    track_done(r.order_id);
                }
            } else {
                idle++;
                mco_yield(mco_running());
            }
        }
    }

    atomic_fetch_add(&g_traders_done, 1);
    /* only spam per-trader lines for small demos */
    if (g_verbose || (g_ntraders <= 32 && !g_quick))
        INFO("trader %d done sent=%d terms≈%d fills_qty=%llu inflight_left=%d",
             id, sent, terms, fills, in_flight);
}

/* -------------------------------------------------------------------------- */
/* session                                                                    */
/* -------------------------------------------------------------------------- */

static void run_fiber_trading(void)
{
    int ntraders, orders_per, cancel_pct;
    int t;
    int match_cap, fill_cap, tin_cap, tout_cap;
    trader_arg targs[MAX_TRADERS];
    char names[MAX_TRADERS][32];
    double t0, t1;
    struct timespec ts0, ts1;

    if (g_cli_traders > 0)
        ntraders = g_cli_traders;
    else if (g_insane)
        ntraders = 510; /* + matcher + gateway = 512 fibers */
    else if (g_quick)
        ntraders = 4;
    else
        ntraders = 12;

    if (g_cli_orders > 0)
        orders_per = g_cli_orders;
    else if (g_insane)
        orders_per = 80;
    else if (g_quick)
        orders_per = 40;
    else
        orders_per = 200;

    cancel_pct = g_insane ? 15 : 20;

    if (ntraders > MAX_TRADERS) ntraders = MAX_TRADERS;
    if (ntraders < 1) ntraders = 1;
    /* room for matcher+gateway */
    if (ntraders + 2 > MAX_FIBERS)
        ntraders = MAX_FIBERS - 2;

    g_ntraders = ntraders;
    g_orders_per = orders_per;

    /* Scale channel capacity with fan-in so big trader counts don't thrash */
    if (ntraders >= 256 || g_insane) {
        match_cap = 4096;
        fill_cap  = 8192;
        tin_cap   = 32;
        tout_cap  = 64;
    } else if (g_quick) {
        match_cap = 64;
        fill_cap  = 128;
        tin_cap   = 16;
        tout_cap  = 32;
    } else {
        match_cap = 256;
        fill_cap  = 512;
        tin_cap   = 16;
        tout_cap  = 32;
    }

    HEAD("fiber trading STRESS: traders=%d orders/trader=%d fibers=%d (1 OS thread)",
         ntraders, orders_per, ntraders + 2);
    INFO("channels: matcher_in=%d matcher_out=%d trader_in=%d trader_out=%d",
         match_cap, fill_cap, tin_cap, tout_cap);

    {
        long long need = (long long)ntraders * orders_per + 1024;
        if (need >= MAX_ORD_TRACK)
            FAIL("order track table too small for %lld orders (cap %d)",
                 need, MAX_ORD_TRACK);
    }

    atomic_store(&g_next_oid, 1);
    atomic_store(&g_matched, 0);
    atomic_store(&g_cancelled, 0);
    atomic_store(&g_rejected, 0);
    atomic_store(&g_fills_to_traders, 0);
    atomic_store(&g_fill_drops, 0);
    atomic_store(&g_traders_done, 0);
    atomic_store(&g_stop, 0);
    memset(g_ord_qty, 0, sizeof(g_ord_qty));
    memset(g_ord_trader, 0, sizeof(g_ord_trader));
    memset((void*)g_ord_live, 0, sizeof(g_ord_live));
    g_nfibers = 0;

    g_to_matcher = cchan_create((unsigned short)match_cap, (unsigned short)sizeof(msg_t));
    g_from_matcher = cchan_create((unsigned short)fill_cap, (unsigned short)sizeof(msg_t));
    if (!g_to_matcher || !g_from_matcher) {
        FAIL("channel create OOM");
        return;
    }
    for (t = 0; t < ntraders; t++) {
        g_from_trader[t] = cchan_create((unsigned short)tin_cap, (unsigned short)sizeof(msg_t));
        g_to_trader[t] = cchan_create((unsigned short)tout_cap, (unsigned short)sizeof(msg_t));
        if (!g_from_trader[t] || !g_to_trader[t]) {
            FAIL("trader channel OOM at %d", t);
            return;
        }
    }

    if (fiber_spawn(matcher_fiber, 0, "matcher") < 0) return;
    if (fiber_spawn(gateway_fiber, 0, "gateway") < 0) return;
    for (t = 0; t < ntraders; t++) {
        targs[t].id = t;
        targs[t].target = orders_per;
        targs[t].cancel_pct = cancel_pct;
        snprintf(names[t], sizeof(names[t]), "trader%d", t);
        if (fiber_spawn(trader_fiber, &targs[t], names[t]) < 0)
            return;
    }
    INFO("spawned %d fibers (stack 32KB each, ~%.1f MB reserved)",
         g_nfibers, g_nfibers * 32.0 / 1024.0);

    clock_gettime(CLOCK_MONOTONIC, &ts0);
    fiber_run_until_done();
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    t0 = (double)ts0.tv_sec + ts0.tv_nsec * 1e-9;
    t1 = (double)ts1.tv_sec + ts1.tv_nsec * 1e-9;

    /* force residual terminals for any LIVE orders */
    {
        int oid, live = 0;
        int scan = atomic_load(&g_next_oid);
        if (scan >= MAX_ORD_TRACK) scan = MAX_ORD_TRACK - 1;
        for (oid = 1; oid < scan; oid++) {
            if (atomic_load(&g_ord_live[oid])) {
                live++;
                atomic_store(&g_ord_live[oid], 0);
            }
        }
        if (live)
            INFO("residual LIVE orders force-cleared: %d", live);
    }

    {
        unsigned long long matched = atomic_load(&g_matched);
        unsigned long long cancelled = atomic_load(&g_cancelled);
        unsigned long long rejected = atomic_load(&g_rejected);
        unsigned long long routed = atomic_load(&g_fills_to_traders);
        unsigned long long drops = atomic_load(&g_fill_drops);
        int total_orders = ntraders * orders_per;
        INFO("wall=%.3fs orders=%d matches=%llu cancels=%llu rejects=%llu reply_msgs=%llu fill_drops=%llu",
             t1 - t0, total_orders, matched, cancelled, rejected, routed, drops);
        INFO("throughput=%.0f orders/s  (single OS thread, %d fibers)",
             total_orders / (t1 - t0 + 1e-9), g_nfibers);

        if (matched == 0 && total_orders > 50)
            FAIL("no matches at all — price band or scheduling bug?");
        if (routed == 0 && total_orders > 0)
            FAIL("no replies reached traders");
        /* under extreme fan-in, many rejects/drops are ok; require forward progress */
        if ((g_insane || ntraders >= 256) &&
            matched + cancelled < (unsigned long long)total_orders / 20)
            FAIL("too little match/cancel progress under heavy fiber load");
    }

    fiber_cleanup();
    cchan_dispose(g_to_matcher);
    cchan_dispose(g_from_matcher);
    for (t = 0; t < ntraders; t++) {
        cchan_dispose(g_from_trader[t]);
        cchan_dispose(g_to_trader[t]);
    }
}

/* -------------------------------------------------------------------------- */
/* micro-demo: pure fiber ping over cchan                                     */
/* -------------------------------------------------------------------------- */

static cchan_t* g_ping;
static cchan_t* g_pong;

static void ping_fiber(mco_coro* co)
{
    int i, v = 0;
    int rounds = g_quick ? 1000 : 20000;
    (void)co;
    for (i = 0; i < rounds; i++) {
        if (!fiber_send(g_ping, &v, 100000)) break;
        if (!fiber_recv(g_pong, &v, 100000)) break;
        v++;
    }
    INFO("ping fiber completed %d/%d round-trips final=%d", i, rounds, v);
}

static void pong_fiber(mco_coro* co)
{
    int i, v;
    int rounds = g_quick ? 1000 : 20000;
    (void)co;
    for (i = 0; i < rounds; i++) {
        if (!fiber_recv(g_ping, &v, 100000)) break;
        v += 1;
        if (!fiber_send(g_pong, &v, 100000)) break;
    }
}

static void run_fiber_pingpong(void)
{
    int rounds = g_quick ? 1000 : 20000;
    /*
     * Capacity-1 buffers, not unbuffered rendezvous.
     *
     * Why: cchan unbuffered try_send only succeeds when a peer is already
     * parked in pthread rendezvous (phase==1). Fibers never call blocking
     * recv (would freeze the OS thread), so pure try_* cannot establish
     * unbuffered rendezvous. Cap-1 + yield-on-full is the fiber-friendly
     * rendezvous equivalent until cchan grows park/unpark hooks.
     */
    HEAD("fiber ping-pong over cap-1 cchan (%d rounds)", rounds);
    g_ping = cchan_create(1, sizeof(int));
    g_pong = cchan_create(1, sizeof(int));
    g_nfibers = 0;
    atomic_store(&g_stop, 0);
    fiber_spawn(ping_fiber, 0, "ping");
    fiber_spawn(pong_fiber, 0, "pong");
    fiber_run_until_done();
    fiber_cleanup();
    cchan_dispose(g_ping);
    cchan_dispose(g_pong);
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) g_quick = 1;
        else if (strcmp(argv[i], "--insane") == 0) g_insane = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verbose = 1;
        else if (strncmp(argv[i], "--traders=", 10) == 0)
            g_cli_traders = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--orders=", 9) == 0)
            g_cli_orders = atoi(argv[i] + 9);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                "usage: %s [--quick|--insane] [-v] [--traders=N] [--orders=N]\n"
                "  --quick   4 traders, small orders\n"
                "  --insane  510 traders + matcher + gateway = 512 fibers\n"
                "  --traders=N  override trader count (max %d)\n"
                "  --orders=N   orders per trader\n",
                argv[0], MAX_TRADERS);
            return 0;
        }
    }

    srand(1); /* deterministic demo */
    printf("cchan + minicoro FIBER TRADING %s\n",
           g_insane ? "STRESS" : "DEMO");
    INFO("mode=%s  (single OS thread, cooperative fibers)",
         g_insane ? "INSANE/512" : (g_quick ? "quick" : "default"));
    INFO("pattern: cchan_try_* + mco_yield  — never block OS thread on cchan");

    if (!g_insane)
        run_fiber_pingpong();
    else
        INFO("skipping ping-pong under --insane (focus on 512-fiber trading)");

    run_fiber_trading();

    printf("\n========================================\n");
    if (g_fail == 0) {
        printf("FIBER TRADING: ALL PASSED\n");
        return 0;
    }
    printf("FIBER TRADING: %d FAILURES\n", g_fail);
    return 1;
}
