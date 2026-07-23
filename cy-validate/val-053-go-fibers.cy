/* REQUIRES: -ffibers */
/* val-053-go-fibers.cy — go / await / Chan<T> / add_scheduler
 *
 * Covers:
 *   1. go + buffered Chan, main consumes (single-thread scheduler)
 *   2. go value pack: pointer + integer args
 *   3. await as pure yield in a try_recv poll loop
 *   4. Unbuffered rendezvous between two fibers on one worker
 *   5. send-on-closed throws RuntimeException; close+drain ok-idiom
 *   6. Multi-worker pipeline (add_scheduler(4)) with atomic counter
 *
 * Run:  ./bin/classyc -g -I include -ffibers cy-validate/val-053-go-fibers.cy -eg
 * AOT:  ./classyc-aot.sh -I include -ffibers cy-validate/val-053-go-fibers.cy -o /tmp/val53 && /tmp/val53
 */
#include "chan.h"
#include <stdatomic.h>

static int fails = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); } \
    else { printf("FAIL %s\n", name); fails++; } \
} while (0)

/* ── 1&2: producer with pointer + int args in the go pack ──────────────── */
void producer(Chan<int> *ch, long base, long n) {
    for (long i = 1; i <= n; i++) ch->send((int)(base + i));
    ch->close();
}

/* ── 4: rendezvous pong fiber ──────────────────────────────────────────── */
void pong(Chan<int> *in, Chan<int> *out) {
    int v = 0;
    while (in->recv(&v)) out->send(v + 1);
    out->close();
}

/* ── 6: multi-worker pipeline ──────────────────────────────────────────── */
static Chan<long> *mw_in;
static Chan<long> *mw_out;
static atomic_llong mw_sum;

void mw_produce(long n) {
    for (long i = 1; i <= n; i++) mw_in->send(i);
    mw_in->close();
}

void mw_double(void) {
    long v = 0;
    while (mw_in->recv(&v)) {
        mw_out->send(v * 2);
        await;
    }
    mw_out->close();
}

void mw_sink(void) {
    long v = 0;
    while (mw_out->recv(&v)) atomic_fetch_add(&mw_sum, v);
}

int main(void) {
    /* 1. buffered channel, main consumes via park (scheduler steps fiber) */
    {
        auto ch = new Chan<int>(4);
        go producer(ch, 0, 100);
        int sum = 0, v = 0;
        while (ch->recv(&v)) sum += v;
        add_scheduler(1);
        CHECK(sum == 5050, "buffered pipeline sum==5050");
        delete ch;
        cy_sched_shutdown();
    }

    /* 2. go value pack delivered base+n correctly (covered above via 5050,
       here with a non-zero base and explicit await in the consumer) */
    {
        auto ch = new Chan<int>(2);
        go producer(ch, 100, 5);
        int sum = 0, v = 0;
        for (;;) {
            int rc = ch->try_recv(&v);
            if (rc < 0) break;
            if (rc == 0) { await; continue; }   /* fiber state check + yield */
            sum += v;
        }
        add_scheduler(1);
        CHECK(sum == 515, "go pack (ptr,int,int) + await poll sum==515");
        delete ch;
        cy_sched_shutdown();
    }

    /* 3. unbuffered rendezvous fiber↔fiber on one worker */
    {
        auto in = new Chan<int>();     /* capacity 0: rendezvous */
        auto out = new Chan<int>();
        go pong(in, out);
        int ok = 1;
        for (int i = 0; i < 50; i++) {
            in->send(i);
            int v = -1;
            if (!out->recv(&v) || v != i + 1) { ok = 0; break; }
        }
        in->close();
        add_scheduler(1);
        CHECK(ok, "rendezvous ping-pong x50");
        delete in;
        delete out;
        cy_sched_shutdown();
    }

    /* 4. send-on-closed throws; double close throws; drain idiom */
    {
        auto ch = new Chan<int>(2);
        ch->send(7);
        ch->close();
        int v = 0, n = 0, last = -1;
        while (ch->recv(&v)) { n++; last = v; }   /* drains 7, then false (v←0) */
        int threw_send = 0, threw_close = 0;
        try { ch->send(1); } catch (Exception e) { threw_send = 1; }
        try { ch->close(); } catch (Exception e) { threw_close = 1; }
        CHECK(n == 1 && last == 7, "close+drain ok-idiom");
        CHECK(threw_send, "send on closed channel throws");
        CHECK(threw_close, "double close throws");
        delete ch;
    }

    /* 5. multi-worker pipeline (4 pthread workers) */
    mw_in = new Chan<long>(16);
    mw_out = new Chan<long>(16);
    atomic_store(&mw_sum, 0LL);
    go mw_produce(1000);
    go mw_double();
    go mw_sink();
    add_scheduler(4);
    {
        long long want = 2LL * 1000 * 1001 / 2;
        CHECK(atomic_load(&mw_sum) == want, "4-worker pipeline sum==1001000");
    }
    delete mw_in;
    delete mw_out;
    cy_sched_shutdown();

    printf(fails == 0 ? "val-053 PASSED\n" : "val-053 FAILED (%d)\n", fails);
    return fails;
}
