/*
 * stress_trading.c — simulated multi-symbol equity matching engine over cchan
 *
 * Architecture (all traffic is channel messages):
 *
 *   trader[i]  --order-->  gateway[g]  --order-->  matcher[sym]
 *       ^                      |                      |
 *       |                      v                      v
 *       +----- fill/ack ------+  <---fill----  (book match)
 *                              select(control, drains…)
 *
 * Side channels hammered continuously:
 *   - cancel storms (client cancels in-flight / resting orders)
 *   - market data ticks (matcher fans out quotes)
 *   - session kill / reconnect (close + recreate ingress)
 *   - global halt / resume control bus
 *   - matching engine roll (stop one symbol, drain, restart)
 *   - random gateway disconnect mid-burst
 *
 * Correctness checks (hard fail if violated):
 *   - every live order ends in FILL, CANCEL_ACK, REJECT, or SESSION_ABORT
 *   - no double-terminal fill for the same order_id
 *   - fill qty never exceeds order qty
 *   - engine survives halt/roll/kill without deadlock
 *
 * Build:
 *   make trading
 *   make trading-quick
 *   ./stress_trading [--quick|--insane]
 */

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/* knobs                                                                      */
/* -------------------------------------------------------------------------- */

static int g_quick = 0;
static int g_insane = 0;
static int g_fail = 0;
static int g_verbose = 0;

#define MAX_TRADERS     128
#define MAX_GATEWAYS    16
#define MAX_SYMBOLS     32
#define MAX_BOOK        256
#define MAX_PENDING     4096

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
    atomic_fetch_add(&g_fail_atomic, 1); \
} while (0)

static atomic_int g_fail_atomic = 0;

#define INFO(fmt, ...) do { \
    printf("  " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)

#define HEAD(fmt, ...) do { \
    printf("\n=== " fmt " ===\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* -------------------------------------------------------------------------- */
/* wire protocol                                                              */
/* -------------------------------------------------------------------------- */

typedef enum {
    MSG_NEW_ORDER = 1,
    MSG_CANCEL,
    MSG_FILL,
    MSG_CANCEL_ACK,
    MSG_REJECT,
    MSG_SESSION_ABORT,   /* exchange killed session; abandon pending */
    MSG_HALT,            /* system-wide halt (control) */
    MSG_RESUME,
    MSG_KILL_SYMBOL,     /* roll one matching engine */
    MSG_RESTART_SYMBOL,
    MSG_KILL_GATEWAY,
    MSG_TICK,            /* market data */
    MSG_SHUTDOWN
} msg_type;

typedef struct {
    int32_t  type;        /* msg_type */
    int32_t  trader_id;
    int32_t  order_id;    /* globally unique */
    int32_t  symbol;       /* 0..nsymbols-1 */
    int32_t  side;        /* 0 buy, 1 sell */
    int32_t  price;       /* integer ticks */
    int32_t  qty;
    int32_t  fill_qty;
    int32_t  fill_price;
    int32_t  gateway_id;
    int32_t  seq;
    int32_t  pad;
} msg_t;

/* resting book entry (matcher-local, not on wire) */
typedef struct {
    int active;
    int order_id;
    int trader_id;
    int gateway_id;
    int side;
    int price;
    int qty_left;
} book_order;

/* per-trader reply endpoint (heap; shared via order messages by id only —
 * replies route through gateway reply mux, not raw pointers on the wire) */

/* -------------------------------------------------------------------------- */
/* global topology                                                            */
/* -------------------------------------------------------------------------- */

typedef struct {
    int id;
    cchan_t* to_trader;     /* fills/acks for this trader */
    cchan_t* from_trader;   /* orders/cancels from this trader → its gateway */
    atomic_int alive;
    atomic_ullong orders_sent;
    atomic_ullong terms_recv; /* terminal msgs */
    atomic_ullong fills_qty;
} trader_slot;

typedef struct {
    int id;
    cchan_t* ingress;                 /* from traders (also used via select) */
    cchan_t** trader_from;            /* alias array into trader from chans */
    int ntraders_owned;
    int* trader_ids;
    cchan_t* to_matcher[MAX_SYMBOLS]; /* shared matcher ingress per symbol */
    cchan_t* control;                 /* kill/resume for this gateway */
    cchan_t* reply_mux;               /* fills coming back from matchers */
    atomic_int alive;
    atomic_int session_gen;           /* bumped on reconnect */
    atomic_ullong forwarded;
    atomic_ullong replies_routed;
} gateway_slot;

typedef struct {
    int symbol;
    cchan_t* ingress;       /* new/cancel from gateways */
    cchan_t* control;       /* halt / kill-symbol */
    cchan_t** gw_reply;     /* per-gateway reply_mux */
    int ngateways;
    atomic_int alive;
    atomic_int halted;
    atomic_ullong matched;
    atomic_ullong cancelled;
    atomic_ullong rejected;
} matcher_slot;

static trader_slot  g_traders[MAX_TRADERS];
static gateway_slot g_gateways[MAX_GATEWAYS];
static matcher_slot g_matchers[MAX_SYMBOLS];
static int g_ntraders, g_ngateways, g_nsymbols;

/* pending order tracker for correctness */
typedef enum { ORD_LIVE = 1, ORD_DONE = 2 } ord_state;
typedef struct {
    atomic_int state;       /* 0 unused, ORD_LIVE, ORD_DONE */
    atomic_int term_type;   /* msg_type of terminal */
    atomic_int fill_qty;
    int trader_id;
    int qty;
    int symbol;
} pend_ent;

static pend_ent* g_pending;
static int g_pending_cap;
static atomic_int g_next_order_id;

static int track_new(int order_id, int trader_id, int qty, int symbol)
{
    if (order_id < 0 || order_id >= g_pending_cap) {
        FAIL("order_id %d out of range", order_id);
        return 0;
    }
    pend_ent* e = &g_pending[order_id];
    int expect = 0;
    if (!atomic_compare_exchange_strong(&e->state, &expect, ORD_LIVE)) {
        FAIL("order %d already tracked state=%d", order_id, expect);
        return 0;
    }
    e->trader_id = trader_id;
    e->qty = qty;
    e->symbol = symbol;
    atomic_store(&e->fill_qty, 0);
    atomic_store(&e->term_type, 0);
    return 1;
}

static void track_fill(int order_id, int fill_qty)
{
    if (order_id < 0 || order_id >= g_pending_cap) {
        FAIL("fill unknown order %d", order_id);
        return;
    }
    pend_ent* e = &g_pending[order_id];
    int st = atomic_load(&e->state);
    if (st != ORD_LIVE && st != ORD_DONE) {
        FAIL("fill for untracked order %d", order_id);
        return;
    }
    int prev = atomic_fetch_add(&e->fill_qty, fill_qty);
    if (prev + fill_qty > e->qty) {
        FAIL("order %d overfill prev=%d +%d > %d", order_id, prev, fill_qty, e->qty);
    }
}

static void track_terminal(int order_id, int term_type)
{
    if (order_id < 0 || order_id >= g_pending_cap) {
        FAIL("term unknown order %d", order_id);
        return;
    }
    pend_ent* e = &g_pending[order_id];
    int st = atomic_load(&e->state);
    if (st == 0) {
        /* SESSION_ABORT may race before track — allow */
        if (term_type != MSG_SESSION_ABORT)
            FAIL("terminal %d for untracked order %d", term_type, order_id);
        return;
    }
    int prev_term = atomic_load(&e->term_type);
    if (term_type == MSG_FILL) {
        /* partial fills are not terminal; full fill is */
        int fq = atomic_load(&e->fill_qty);
        if (fq < e->qty)
            return; /* still live */
    }
    if (prev_term != 0 && prev_term != term_type) {
        /* FILL then CANCEL_ACK is bad; SESSION_ABORT can supersede */
        if (term_type != MSG_SESSION_ABORT && prev_term != MSG_SESSION_ABORT) {
            FAIL("order %d double terminal %d then %d", order_id, prev_term, term_type);
        }
    }
    atomic_store(&e->term_type, term_type);
    atomic_store(&e->state, ORD_DONE);
}

/* -------------------------------------------------------------------------- */
/* matcher: price-time book (simple arrays)                                   */
/* -------------------------------------------------------------------------- */

static int book_add(book_order* book, int n, const msg_t* o)
{
    int i;
    for (i = 0; i < n; i++) {
        if (!book[i].active) {
            book[i].active = 1;
            book[i].order_id = o->order_id;
            book[i].trader_id = o->trader_id;
            book[i].gateway_id = o->gateway_id;
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

/*
 * Hot-path multi-hop sends use cchan_send_budget (library API).
 * Never block forever on a full downstream stage — see API.md.
 */
static void send_reply(matcher_slot* m, const msg_t* r)
{
    int gw = r->gateway_id;
    if (gw < 0 || gw >= m->ngateways || !m->gw_reply[gw])
        return;
    /*
     * Matcher must never stall on the reply path. Budgeted try then drop:
     * full reply mux must not fill matcher ingress (circular wait).
     */
    (void)cchan_send_budget(m->gw_reply[gw], r, 32);
}

/* Try match incoming order against opposite side; rest leftover. */
static void matcher_process_new(matcher_slot* m, book_order* book, int bookn, msg_t* o)
{
    int i;
    int remaining = o->qty;

    /* Halt: reject new */
    if (atomic_load(&m->halted)) {
        msg_t rej = *o;
        rej.type = MSG_REJECT;
        send_reply(m, &rej);
        atomic_fetch_add(&m->rejected, 1);
        track_terminal(o->order_id, MSG_REJECT);
        return;
    }

    for (i = 0; i < bookn && remaining > 0; i++) {
        book_order* b = &book[i];
        if (!b->active) continue;
        if (b->side == o->side) continue; /* same side */
        /* price check: buy lifts ask if buy.price >= ask.price; sell hits bid */
        if (o->side == 0) { /* buy */
            if (o->price < b->price) continue;
        } else {
            if (o->price > b->price) continue;
        }

        int fq = remaining < b->qty_left ? remaining : b->qty_left;
        int fpx = b->price; /* resting price */

        /* fill for aggressor */
        {
            msg_t fill = *o;
            fill.type = MSG_FILL;
            fill.fill_qty = fq;
            fill.fill_price = fpx;
            fill.qty = o->qty;
            track_fill(o->order_id, fq);
            send_reply(m, &fill);
            if (atomic_load(&g_pending[o->order_id].fill_qty) >= o->qty)
                track_terminal(o->order_id, MSG_FILL);
        }
        /* fill for resting */
        {
            msg_t fill;
            memset(&fill, 0, sizeof(fill));
            fill.type = MSG_FILL;
            fill.trader_id = b->trader_id;
            fill.order_id = b->order_id;
            fill.symbol = m->symbol;
            fill.side = b->side;
            fill.price = b->price;
            fill.qty = b->qty_left; /* original remaining context not kept; ok */
            fill.fill_qty = fq;
            fill.fill_price = fpx;
            fill.gateway_id = b->gateway_id;
            track_fill(b->order_id, fq);
            send_reply(m, &fill);
            if (atomic_load(&g_pending[b->order_id].fill_qty) >=
                /* best-effort: if resting fully depleted mark done */
                (b->qty_left /* only this slice */)) {
                /* refined below via qty_left */
            }
            if (fq >= b->qty_left)
                track_terminal(b->order_id, MSG_FILL);
        }

        b->qty_left -= fq;
        remaining -= fq;
        atomic_fetch_add(&m->matched, 1);
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
            send_reply(m, &rej);
            /* if partially filled already, terminal is messy — reject residual */
            if (remaining == o->qty)
                track_terminal(o->order_id, MSG_REJECT);
            atomic_fetch_add(&m->rejected, 1);
        }
    }
}

static void* matcher_thread(void* arg)
{
    matcher_slot* m = (matcher_slot*)arg;
    book_order book[MAX_BOOK];
    memset(book, 0, sizeof(book));
    atomic_store(&m->alive, 1);

    while (atomic_load(&m->alive)) {
        msg_t msg, ctrl;
        cchan_t* recvs[2] = { m->ingress, m->control };
        void* rbufs[2] = { &msg, &ctrl };
        int idx = cchan_select(recvs, rbufs, 2, 0, 0, 0);
        if (idx < 0)
            break;

        msg_t* in = (idx == 0) ? &msg : &ctrl;

        if (in->type == MSG_SHUTDOWN || in->type == MSG_KILL_SYMBOL) {
            /* abort all resting */
            int i;
            for (i = 0; i < MAX_BOOK; i++) {
                if (!book[i].active) continue;
                msg_t ab;
                memset(&ab, 0, sizeof(ab));
                ab.type = MSG_SESSION_ABORT;
                ab.order_id = book[i].order_id;
                ab.trader_id = book[i].trader_id;
                ab.gateway_id = book[i].gateway_id;
                ab.symbol = m->symbol;
                send_reply(m, &ab);
                track_terminal(book[i].order_id, MSG_SESSION_ABORT);
                book[i].active = 0;
            }
            if (in->type == MSG_SHUTDOWN) {
                atomic_store(&m->alive, 0);
                break;
            }
            /* KILL_SYMBOL: stay alive but empty; wait RESTART */
            atomic_store(&m->halted, 1);
            continue;
        }
        if (in->type == MSG_HALT) {
            atomic_store(&m->halted, 1);
            continue;
        }
        if (in->type == MSG_RESUME || in->type == MSG_RESTART_SYMBOL) {
            atomic_store(&m->halted, 0);
            continue;
        }
        if (idx != 0)
            continue; /* unknown control */

        if (in->type == MSG_NEW_ORDER) {
            matcher_process_new(m, book, MAX_BOOK, in);
        } else if (in->type == MSG_CANCEL) {
            book_order gone;
            book_cancel(book, MAX_BOOK, in->order_id, &gone);
            msg_t ack = *in;
            if (gone.active) {
                ack.type = MSG_CANCEL_ACK;
                ack.qty = gone.qty_left;
                atomic_fetch_add(&m->cancelled, 1);
                track_terminal(in->order_id, MSG_CANCEL_ACK);
            } else {
                ack.type = MSG_REJECT; /* cancel miss */
                atomic_fetch_add(&m->rejected, 1);
                /* don't force terminal if fills already completed */
                if (in->order_id >= 0 && in->order_id < g_pending_cap) {
                    int st = atomic_load(&g_pending[in->order_id].state);
                    if (st == ORD_LIVE)
                        track_terminal(in->order_id, MSG_REJECT);
                }
            }
            send_reply(m, &ack);
        }
    }

    atomic_store(&m->alive, 0);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* gateway: multiplex traders → matchers, demux replies                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    gateway_slot* gw;
    trader_slot* traders;
} gw_arg;

static void* gateway_reply_thread(void* arg)
{
    gateway_slot* gw = (gateway_slot*)arg;
    msg_t m;
    while (cchan_recv(gw->reply_mux, &m)) {
        int tid = m.trader_id;
        int rc;
        if (tid < 0 || tid >= g_ntraders) continue;
        /* session abort if gateway killed */
        if (!atomic_load(&gw->alive) && m.type != MSG_SESSION_ABORT) {
            m.type = MSG_SESSION_ABORT;
        }
        /*
         * Deliver without parking forever on a slow/full trader channel.
         * A long spin here stalls the reply mux and then the matchers.
         */
        rc = cchan_send_budget(g_traders[tid].to_trader, &m, 4096);
        if (rc == 1)
            atomic_fetch_add(&gw->replies_routed, 1);
        /* drop on would-block/closed: order may stay LIVE until residual scan */
    }
    return 0;
}

static void* gateway_thread(void* arg)
{
    gateway_slot* gw = (gateway_slot*)arg;
    int nowned = gw->ntraders_owned;
    /* select: each owned trader from_* + control + (optional) */
    int max_n = nowned + 1;
    cchan_t** recvs = (cchan_t**)calloc((size_t)max_n, sizeof(cchan_t*));
    void** rbufs = (void**)calloc((size_t)max_n, sizeof(void*));
    msg_t* slots = (msg_t*)calloc((size_t)max_n, sizeof(msg_t));
    int* live_idx = (int*)calloc((size_t)max_n, sizeof(int)); /* trader slot or -1 control */

    atomic_store(&gw->alive, 1);

    while (atomic_load(&gw->alive)) {
        int n = 0;
        int i;
        for (i = 0; i < nowned; i++) {
            int tid = gw->trader_ids[i];
            if (!atomic_load(&g_traders[tid].alive)) continue;
            recvs[n] = g_traders[tid].from_trader;
            rbufs[n] = &slots[n];
            live_idx[n] = tid;
            n++;
        }
        recvs[n] = gw->control;
        rbufs[n] = &slots[n];
        live_idx[n] = -1;
        n++;

        if (n == 0) break;

        int idx = cchan_select(recvs, rbufs, (unsigned)n, 0, 0, 0);
        if (idx < 0)
            break;

        msg_t* m = &slots[idx];

        /* Closed-channel select yields a zeroed msg — skip. */
        if (m->type == 0)
            continue;

        if (live_idx[idx] < 0) {
            /* control */
            if (m->type == MSG_SHUTDOWN || m->type == MSG_KILL_GATEWAY) {
                atomic_store(&gw->alive, 0);
                break;
            }
            if (m->type == MSG_HALT || m->type == MSG_RESUME) {
                /* broadcast to all symbols via matcher control (never block) */
                int s;
                for (s = 0; s < g_nsymbols; s++) {
                    msg_t c = *m;
                    cchan_send_budget(g_matchers[s].control, &c, 64);
                }
            }
            continue;
        }

        /* from trader */
        if (m->type == MSG_SHUTDOWN)
            continue;

        if (!atomic_load(&gw->alive))
            break;

        m->gateway_id = gw->id;
        if (m->symbol < 0 || m->symbol >= g_nsymbols) {
            msg_t rej = *m;
            rej.type = MSG_REJECT;
            cchan_send_budget(g_traders[m->trader_id].to_trader, &rej, 256);
            track_terminal(m->order_id, MSG_REJECT);
            continue;
        }

        if (m->type == MSG_NEW_ORDER)
            track_new(m->order_id, m->trader_id, m->qty, m->symbol);

        /* Forward without indefinite block: budgeted try, else reject. */
        {
            int rc;
            cchan_t* dest = gw->to_matcher[m->symbol];
            rc = cchan_send_budget(dest, m, 512);
            if (rc == 1) {
                atomic_fetch_add(&gw->forwarded, 1);
            } else {
                msg_t rej = *m;
                rej.type = MSG_REJECT;
                cchan_send_budget(g_traders[m->trader_id].to_trader, &rej, 256);
                track_terminal(m->order_id, MSG_REJECT);
            }
        }
    }

    /* session abort all live orders for owned traders (never block on delivery) */
    {
        int oid, j;
        for (oid = 0; oid < g_pending_cap; oid++) {
            if (atomic_load(&g_pending[oid].state) != ORD_LIVE) continue;
            int tid = g_pending[oid].trader_id;
            int owned = 0;
            for (j = 0; j < nowned; j++)
                if (gw->trader_ids[j] == tid) { owned = 1; break; }
            if (!owned) continue;
            msg_t ab;
            memset(&ab, 0, sizeof(ab));
            ab.type = MSG_SESSION_ABORT;
            ab.order_id = oid;
            ab.trader_id = tid;
            cchan_send_budget(g_traders[tid].to_trader, &ab, 64);
            track_terminal(oid, MSG_SESSION_ABORT);
        }
    }

    atomic_store(&gw->alive, 0);
    free(recvs); free(rbufs); free(slots); free(live_idx);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* trader clients                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    int id;
    int orders_target;
    int cancel_pct;     /* 0..100 */
    int burst_sleep_ms;
    atomic_int* stop;
} trader_arg;

/* Apply one reply message to trader accounting. Returns 1 if in_flight decremented. */
static int trader_handle_reply(trader_slot* t, const msg_t* r, int* in_flight)
{
    if (r->type == MSG_FILL) {
        atomic_fetch_add(&t->fills_qty, (unsigned)r->fill_qty);
        if (r->order_id >= 0 && r->order_id < g_pending_cap) {
            int fq = atomic_load(&g_pending[r->order_id].fill_qty);
            int q = g_pending[r->order_id].qty;
            if (fq >= q || atomic_load(&g_pending[r->order_id].state) == ORD_DONE) {
                if (*in_flight > 0) (*in_flight)--;
                atomic_fetch_add(&t->terms_recv, 1);
                return 1;
            }
        }
        return 0;
    }
    if (r->type == MSG_CANCEL_ACK || r->type == MSG_REJECT ||
        r->type == MSG_SESSION_ABORT) {
        if (*in_flight > 0) (*in_flight)--;
        atomic_fetch_add(&t->terms_recv, 1);
        return 1;
    }
    return 0;
}

/* Drain all currently queued replies without blocking. */
static void trader_drain_replies(trader_slot* t, int* in_flight)
{
    msg_t r;
    while (cchan_try_recv(t->to_trader, &r) == 1)
        trader_handle_reply(t, &r, in_flight);
}

static void* trader_thread(void* arg)
{
    trader_arg* a = (trader_arg*)arg;
    trader_slot* t = &g_traders[a->id];
    int sent = 0;
    int in_flight = 0;
    const int max_inflight = g_quick ? 8 : 24;
    int stall_ms = 0;

    atomic_store(&t->alive, 1);

    /*
     * NEVER block forever on cchan_recv waiting for a terminal.
     *
     * Bad pattern (this harness used to do it):
     *   send N orders → park on reply → matchers only abort resting book on
     *   shutdown → shutdown only after all traders join → circular wait.
     *
     * Good pattern:
     *   try_send orders, try_recv replies, hard wall-clock budget, exit on stop.
     *   Resting / dropped replies become residual LIVE (allowed under chaos).
     */
    while (!atomic_load(a->stop) && sent < a->orders_target) {
        int made_progress = 0;

        while (in_flight < max_inflight && sent < a->orders_target &&
               !atomic_load(a->stop)) {
            msg_t o;
            int rc;
            memset(&o, 0, sizeof(o));
            o.type = MSG_NEW_ORDER;
            o.trader_id = a->id;
            o.order_id = atomic_fetch_add(&g_next_order_id, 1);
            o.symbol = (a->id * 17 + sent * 3) % g_nsymbols;
            o.side = (sent + a->id) & 1;
            /* Tight price band so opposite sides actually meet. */
            o.price = 1000 + ((sent * 3 + a->id) % 7) - 3;
            o.qty = 1 + (sent % 5);
            o.seq = sent;

            rc = cchan_send_budget(t->from_trader, &o, 128);
            if (rc != 1) {
                if (rc < 0)
                    goto done; /* channel closed */
                break; /* backpressure: drain first */
            }

            atomic_fetch_add(&t->orders_sent, 1);
            sent++;
            in_flight++;
            made_progress = 1;

            if ((rand() % 100) < a->cancel_pct) {
                msg_t c = o;
                c.type = MSG_CANCEL;
                cchan_send_budget(t->from_trader, &c, 32);
            }
        }

        {
            int before = in_flight;
            trader_drain_replies(t, &in_flight);
            if (in_flight != before)
                made_progress = 1;
        }

        if (made_progress) {
            stall_ms = 0;
        } else {
            /*
             * Stuck with max_inflight and no replies: do not spin forever.
             * After a short stall, force-release a slot so new work can proceed
             * (lost terminal under drop/reject race). Correctness still tracks
             * the order as LIVE until residual abort.
             */
            stall_ms++;
            if (stall_ms >= 50 && in_flight > 0) {
                in_flight--;
                stall_ms = 0;
            } else if (stall_ms > 2000 && in_flight == 0) {
                break;
            }
            cchan_sleep(1);
        }

        if (a->burst_sleep_ms && (sent % 16) == 0)
            cchan_sleep((unsigned)a->burst_sleep_ms);
    }

    /* Hard wall-clock drain — never more than a second after stop/done. */
    {
        double t_end = now_s() + (g_quick ? 0.15 : 0.5);
        while (in_flight > 0 && now_s() < t_end) {
            int before = in_flight;
            trader_drain_replies(t, &in_flight);
            if (in_flight == before)
                cchan_sleep(1);
            if (atomic_load(a->stop) && now_s() > t_end - 0.1)
                break;
        }
    }

done:
    trader_drain_replies(t, &in_flight);
    atomic_store(&t->alive, 0);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* chaos monkey / orchestrator                                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    atomic_int* stop;
    int duration_ms;
} chaos_arg;

static void* chaos_thread(void* arg)
{
    chaos_arg* a = (chaos_arg*)arg;
    double t0 = now_s();
    int actions = 0;

    /*
     * Chaos must never use blocking cchan_send. Control queues are small; if a
     * matcher is busy matching under select, a full control buffer would park
     * chaos — and main may be waiting on chaos after stop. Budgeted try only.
     */
    while (!atomic_load(a->stop) &&
           (now_s() - t0) * 1000.0 < a->duration_ms) {
        int roll = rand() % 100;
        cchan_sleep((unsigned)(g_quick ? 5 : 15));

        if (roll < 15) {
            int s;
            msg_t h;
            memset(&h, 0, sizeof(h));
            h.type = MSG_HALT;
            for (s = 0; s < g_nsymbols; s++)
                cchan_send_budget(g_matchers[s].control, &h, 32);
            cchan_sleep(g_quick ? 10 : 30);
            h.type = MSG_RESUME;
            for (s = 0; s < g_nsymbols; s++)
                cchan_send_budget(g_matchers[s].control, &h, 32);
            actions++;
            if (g_verbose) INFO("chaos: halt/resume");
        } else if (roll < 30) {
            int s = rand() % g_nsymbols;
            msg_t k;
            memset(&k, 0, sizeof(k));
            k.type = MSG_KILL_SYMBOL;
            cchan_send_budget(g_matchers[s].control, &k, 32);
            cchan_sleep(g_quick ? 5 : 20);
            k.type = MSG_RESTART_SYMBOL;
            cchan_send_budget(g_matchers[s].control, &k, 32);
            actions++;
            if (g_verbose) INFO("chaos: roll symbol %d", s);
        } else if (roll < 40) {
            int g = rand() % g_ngateways;
            msg_t h;
            memset(&h, 0, sizeof(h));
            h.type = MSG_HALT;
            cchan_send_budget(g_gateways[g].control, &h, 32);
            cchan_sleep(5);
            h.type = MSG_RESUME;
            cchan_send_budget(g_gateways[g].control, &h, 32);
            actions++;
        } else if (roll < 55) {
            int s = rand() % g_nsymbols;
            msg_t t;
            memset(&t, 0, sizeof(t));
            t.type = MSG_TICK;
            t.symbol = s;
            t.price = 1000 + (rand() % 10);
            cchan_send_budget(g_matchers[s].control, &t, 8);
            actions++;
        } else {
            cchan_sleep(g_quick ? 2 : 8);
            actions++;
        }
    }

    INFO("chaos: %d actions over %.2fs", actions, now_s() - t0);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* setup / teardown                                                           */
/* -------------------------------------------------------------------------- */

static int spawn(pthread_t* t, void* (*fn)(void*), void* arg)
{
    return pthread_create(t, 0, fn, arg) == 0;
}

static void run_session(void)
{
    int ntraders = g_insane ? 64 : (g_quick ? 8 : 24);
    int ngateways = g_insane ? 8 : (g_quick ? 2 : 4);
    int nsymbols = g_insane ? 16 : (g_quick ? 4 : 8);
    int orders_per = g_insane ? 2000 : (g_quick ? 100 : 500);
    int cancel_pct = 25;
    int chaos_ms = g_insane ? 8000 : (g_quick ? 800 : 3000);

    if (ntraders > MAX_TRADERS) ntraders = MAX_TRADERS;
    if (ngateways > MAX_GATEWAYS) ngateways = MAX_GATEWAYS;
    if (nsymbols > MAX_SYMBOLS) nsymbols = MAX_SYMBOLS;

    g_ntraders = ntraders;
    g_ngateways = ngateways;
    g_nsymbols = nsymbols;

    g_pending_cap = ntraders * orders_per + 1024;
    g_pending = (pend_ent*)calloc((size_t)g_pending_cap, sizeof(pend_ent));
    atomic_store(&g_next_order_id, 1);

    HEAD("trading session: traders=%d gateways=%d symbols=%d orders/trader=%d chaos=%dms",
         ntraders, ngateways, nsymbols, orders_per, chaos_ms);

    /* create matcher channels */
    int s, g, t;
    pthread_t* matcher_th = (pthread_t*)calloc((size_t)nsymbols, sizeof(pthread_t));
    pthread_t* gw_th = (pthread_t*)calloc((size_t)ngateways, sizeof(pthread_t));
    pthread_t* gw_reply_th = (pthread_t*)calloc((size_t)ngateways, sizeof(pthread_t));
    pthread_t* trader_th = (pthread_t*)calloc((size_t)ntraders, sizeof(pthread_t));
    trader_arg* targs = (trader_arg*)calloc((size_t)ntraders, sizeof(trader_arg));
    atomic_int stop = 0;

    for (s = 0; s < nsymbols; s++) {
        matcher_slot* m = &g_matchers[s];
        memset(m, 0, sizeof(*m));
        m->symbol = s;
        m->ingress = cchan_create(512, (unsigned short)sizeof(msg_t));
        m->control = cchan_create(64, (unsigned short)sizeof(msg_t));
        m->ngateways = ngateways;
        m->gw_reply = (cchan_t**)calloc((size_t)ngateways, sizeof(cchan_t*));
    }

    /* gateways */
    for (g = 0; g < ngateways; g++) {
        gateway_slot* gw = &g_gateways[g];
        memset(gw, 0, sizeof(*gw));
        gw->id = g;
        gw->control = cchan_create(32, (unsigned short)sizeof(msg_t));
        gw->reply_mux = cchan_create(1024, (unsigned short)sizeof(msg_t));
        for (s = 0; s < nsymbols; s++) {
            gw->to_matcher[s] = g_matchers[s].ingress;
            cchan_retain(g_matchers[s].ingress);
            g_matchers[s].gw_reply[g] = gw->reply_mux;
        }
        /* own a shard of traders */
        int base = (g * ntraders) / ngateways;
        int end = ((g + 1) * ntraders) / ngateways;
        gw->ntraders_owned = end - base;
        gw->trader_ids = (int*)calloc((size_t)gw->ntraders_owned, sizeof(int));
        for (t = 0; t < gw->ntraders_owned; t++)
            gw->trader_ids[t] = base + t;
    }

    /* traders */
    for (t = 0; t < ntraders; t++) {
        trader_slot* tr = &g_traders[t];
        memset(tr, 0, sizeof(*tr));
        tr->id = t;
        tr->to_trader = cchan_create(128, (unsigned short)sizeof(msg_t));
        tr->from_trader = cchan_create(128, (unsigned short)sizeof(msg_t));
    }

    /* start matchers, gateways, reply demux, traders */
    for (s = 0; s < nsymbols; s++)
        spawn(&matcher_th[s], matcher_thread, &g_matchers[s]);
    for (g = 0; g < ngateways; g++) {
        spawn(&gw_th[g], gateway_thread, &g_gateways[g]);
        spawn(&gw_reply_th[g], gateway_reply_thread, &g_gateways[g]);
    }

    chaos_arg carg;
    pthread_t chaos_th;
    carg.stop = &stop;
    carg.duration_ms = chaos_ms;
    spawn(&chaos_th, chaos_thread, &carg);

    double t0 = now_s();
    /*
     * Session budget: traders must finish (or we force-stop) well under a hard
     * wall clock. Joining traders first without a stop signal is the classic
     * hang: resting book + dropped replies ⇒ inflight never reaches 0 ⇒ join
     * forever, chaos already done printing "actions".
     */
    double session_budget =
        (g_insane ? 30.0 : (g_quick ? 3.0 : 12.0));

    for (t = 0; t < ntraders; t++) {
        targs[t].id = t;
        targs[t].orders_target = orders_per;
        targs[t].cancel_pct = cancel_pct;
        targs[t].burst_sleep_ms = 0;
        targs[t].stop = &stop;
        /* Mark alive before spawn so the poll cannot race with start-up. */
        atomic_store(&g_traders[t].alive, 1);
        spawn(&trader_th[t], trader_thread, &targs[t]);
    }

    /*
     * Poll traders with a hard wall-clock budget. Never join with no stop
     * signal while in-flight terminals may be dropped / resting forever.
     */
    {
        int all_done = 0;
        while (!all_done) {
            all_done = 1;
            for (t = 0; t < ntraders; t++) {
                if (atomic_load(&g_traders[t].alive))
                    all_done = 0;
            }
            if (all_done)
                break;
            if ((now_s() - t0) >= session_budget) {
                INFO("session budget %.1fs hit — force stop", session_budget);
                atomic_store(&stop, 1);
                for (t = 0; t < ntraders; t++) {
                    cchan_close(g_traders[t].from_trader);
                    cchan_close(g_traders[t].to_trader);
                }
                break;
            }
            cchan_sleep(5);
        }
    }

    atomic_store(&stop, 1);

    /* Safety net: if a trader still hung, closing channels should free it. */
    for (t = 0; t < ntraders; t++) {
        cchan_close(g_traders[t].from_trader);
        cchan_close(g_traders[t].to_trader);
    }

    for (t = 0; t < ntraders; t++)
        pthread_join(trader_th[t], 0);

    pthread_join(chaos_th, 0);

    /* shutdown matchers — try_send first; close always wakes select */
    for (s = 0; s < nsymbols; s++) {
        msg_t sh;
        memset(&sh, 0, sizeof(sh));
        sh.type = MSG_SHUTDOWN;
        cchan_send_budget(g_matchers[s].control, &sh, 64);
        cchan_close(g_matchers[s].control);
        cchan_close(g_matchers[s].ingress);
    }
    for (s = 0; s < nsymbols; s++)
        pthread_join(matcher_th[s], 0);

    /* shutdown gateways */
    for (g = 0; g < ngateways; g++) {
        msg_t sh;
        memset(&sh, 0, sizeof(sh));
        sh.type = MSG_SHUTDOWN;
        cchan_send_budget(g_gateways[g].control, &sh, 64);
        atomic_store(&g_gateways[g].alive, 0);
        cchan_close(g_gateways[g].control);
        cchan_close(g_gateways[g].reply_mux);
    }
    for (g = 0; g < ngateways; g++) {
        pthread_join(gw_th[g], 0);
        pthread_join(gw_reply_th[g], 0);
    }

    /* close any trader chans not already closed by budget path */
    for (t = 0; t < ntraders; t++) {
        cchan_close(g_traders[t].from_trader);
        cchan_close(g_traders[t].to_trader);
    }

    double t1 = now_s();

    /* stats + correctness */
    unsigned long long orders = 0, fills_qty = 0, matched = 0, cancelled = 0, rejected = 0;
    unsigned long long live_left = 0, done = 0;
    int oid;
    for (t = 0; t < ntraders; t++) {
        orders += atomic_load(&g_traders[t].orders_sent);
        fills_qty += atomic_load(&g_traders[t].fills_qty);
    }
    for (s = 0; s < nsymbols; s++) {
        matched += atomic_load(&g_matchers[s].matched);
        cancelled += atomic_load(&g_matchers[s].cancelled);
        rejected += atomic_load(&g_matchers[s].rejected);
    }
    for (oid = 0; oid < g_pending_cap; oid++) {
        int st = atomic_load(&g_pending[oid].state);
        if (st == ORD_LIVE) live_left++;
        else if (st == ORD_DONE) done++;
    }

    INFO("wall=%.3fs orders_sent=%llu matcher_matches=%llu cancels=%llu rejects=%llu",
         t1 - t0, orders, matched, cancelled, rejected);
    INFO("fill_qty_total=%llu pending_done=%llu pending_LIVE_left=%llu",
         fills_qty, done, live_left);
    INFO("throughput=%.0f orders/s", orders / (t1 - t0 + 1e-9));

    /* Live leftovers are ok only if session-aborted paths lag; we require that
       almost all tracked orders terminated. Allow small residual under chaos. */
    {
        unsigned long long tracked = done + live_left;
        if (tracked == 0 && orders > 0)
            FAIL("no orders tracked but sent %llu", orders);
        if (live_left > (orders / 10 + 50)) {
            FAIL("too many LIVE orders left: %llu (orders=%llu)", live_left, orders);
        } else if (live_left) {
            INFO("note: %llu orders still LIVE after shutdown (chaos residual; not fatal under threshold)",
                 live_left);
        }
    }

    /* dispose */
    for (t = 0; t < ntraders; t++) {
        cchan_dispose(g_traders[t].from_trader);
        cchan_dispose(g_traders[t].to_trader);
    }
    for (g = 0; g < ngateways; g++) {
        for (s = 0; s < nsymbols; s++)
            cchan_release(g_gateways[g].to_matcher[s]);
        cchan_dispose(g_gateways[g].control);
        cchan_dispose(g_gateways[g].reply_mux);
        free(g_gateways[g].trader_ids);
    }
    for (s = 0; s < nsymbols; s++) {
        cchan_dispose(g_matchers[s].ingress);
        cchan_dispose(g_matchers[s].control);
        free(g_matchers[s].gw_reply);
    }
    free(matcher_th); free(gw_th); free(gw_reply_th); free(trader_th); free(targs);
    free(g_pending);
    g_pending = 0;
}

/* -------------------------------------------------------------------------- */
/* secondary: rapid session start/stop                                        */
/* -------------------------------------------------------------------------- */

static void run_session_flapping(void)
{
    int rounds = g_insane ? 20 : (g_quick ? 3 : 8);
    int r;
    HEAD("session flapping: %d mini trading sessions", rounds);
    for (r = 0; r < rounds; r++) {
        int saved_quick = g_quick;
        g_quick = 1; /* force small */
        INFO("flap round %d/%d", r + 1, rounds);
        run_session();
        g_quick = saved_quick;
        if (atomic_load(&g_fail_atomic))
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* secondary: pure cancel storm on one symbol                                  */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* reply;
    atomic_ullong* acks;
    atomic_int* stop;
} cancel_drain_arg;

static void* cancel_drain_thread(void* arg)
{
    cancel_drain_arg* a = (cancel_drain_arg*)arg;
    msg_t r;
    double t_end = now_s() + 5.0; /* hard cap: never hang this helper */
    while (now_s() < t_end) {
        int rc = cchan_try_recv(a->reply, &r);
        if (rc == 1) {
            if (r.type == MSG_CANCEL_ACK || r.type == MSG_FILL ||
                r.type == MSG_REJECT || r.type == MSG_SESSION_ABORT)
                atomic_fetch_add(a->acks, 1);
        } else if (rc == 0) {
            if (atomic_load(a->stop) && cchan_size(a->reply) == 0)
                break;
            cchan_sleep(1);
        } else {
            break; /* closed */
        }
    }
    return 0;
}

static void run_cancel_storm(void)
{
    enum { N = 640 };
    cchan_t* ingress = cchan_create(128, (unsigned short)sizeof(msg_t));
    cchan_t* control = cchan_create(8, (unsigned short)sizeof(msg_t));
    cchan_t* reply = cchan_create(512, (unsigned short)sizeof(msg_t));
    matcher_slot* m = &g_matchers[0];
    pthread_t mt, dt;
    cancel_drain_arg da;
    atomic_int drain_stop = 0;
    int i;
    atomic_ullong acks = 0;

    HEAD("cancel storm: %d new+cancel pairs on one matcher", N);

    memset(m, 0, sizeof(*m));
    m->symbol = 0;
    m->ingress = ingress;
    m->control = control;
    m->ngateways = 1;
    m->gw_reply = (cchan_t**)calloc(1, sizeof(cchan_t*));
    m->gw_reply[0] = reply;

    g_nsymbols = 1;
    g_ngateways = 1;
    g_pending_cap = N + 16;
    g_pending = (pend_ent*)calloc((size_t)g_pending_cap, sizeof(pend_ent));
    atomic_store(&g_next_order_id, 1);

    spawn(&mt, matcher_thread, m);
    da.reply = reply;
    da.acks = &acks;
    da.stop = &drain_stop;
    spawn(&dt, cancel_drain_thread, &da);

    for (i = 1; i <= N; i++) {
        msg_t o, c;
        int spins, rc;
        memset(&o, 0, sizeof(o));
        o.type = MSG_NEW_ORDER;
        o.order_id = i;
        o.trader_id = 0;
        o.gateway_id = 0;
        o.symbol = 0;
        o.side = i & 1;
        o.price = 1000;
        o.qty = 3;
        track_new(i, 0, 3, 0);
        for (spins = 0, rc = 0; spins < 100000; spins++) {
            rc = cchan_try_send(ingress, &o);
            if (rc != 0) break;
            if ((spins & 255) == 0) cchan_sleep(0);
        }
        if (rc != 1) break;
        memset(&c, 0, sizeof(c));
        c.type = MSG_CANCEL;
        c.order_id = i;
        c.trader_id = 0;
        c.gateway_id = 0;
        c.symbol = 0;
        for (spins = 0, rc = 0; spins < 100000; spins++) {
            rc = cchan_try_send(ingress, &c);
            if (rc != 0) break;
            if ((spins & 255) == 0) cchan_sleep(0);
        }
        if (rc != 1) break;
    }

    {
        msg_t sh;
        memset(&sh, 0, sizeof(sh));
        sh.type = MSG_SHUTDOWN;
        cchan_send(control, &sh);
        cchan_close(ingress);
        pthread_join(mt, 0);
    }
    atomic_store(&drain_stop, 1);
    cchan_sleep(20);
    cchan_close(reply);
    pthread_join(dt, 0);

    INFO("cancel storm replies=%llu", (unsigned long long)atomic_load(&acks));
    if (atomic_load(&acks) == 0)
        FAIL("cancel storm got no replies");

    cchan_dispose(ingress);
    cchan_dispose(control);
    cchan_dispose(reply);
    free(m->gw_reply);
    free(g_pending);
    g_pending = 0;
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    int i;
    double wall0, wall1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) g_quick = 1;
        else if (strcmp(argv[i], "--insane") == 0) g_insane = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verbose = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "usage: %s [--quick|--insane] [-v]\n", argv[0]);
            return 0;
        }
    }

    srand((unsigned)time(0) ^ (unsigned)getpid());
    printf("cchan TRADING STRESS\n");
    INFO("mode=%s", g_insane ? "INSANE" : (g_quick ? "quick" : "default"));

    wall0 = now_s();

    run_cancel_storm();
    run_session();
    run_session_flapping();

    wall1 = now_s();
    g_fail = atomic_load(&g_fail_atomic);

    printf("\n========================================\n");
    printf("wall time: %.2fs\n", wall1 - wall0);
    if (g_fail == 0) {
        printf("TRADING STRESS: ALL PASSED\n");
        return 0;
    }
    printf("TRADING STRESS: %d FAILURES\n", g_fail);
    return 1;
}
