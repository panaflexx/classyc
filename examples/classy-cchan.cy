/* classy-cchan.cy — smoke-test Go-style CSP channels via ext/ccchan
 *
 * Integrates the header-only cchan library with ClassyC + pthreads.
 * Exercises:
 *   1. unbuffered rendezvous send/recv
 *   2. buffered producer → consumer with close / drain (typed i32 helpers)
 *   3. multi-worker fan-in on one channel
 *
 * Run from the project root (all driver options before -eg):
 *
 *   ./bin/classyc -I ext/ccchan -w examples/classy-cchan.cy -eg
 *
 * Notes:
 *   - Define CCHAN_IMPLEMENTATION in exactly one TU (this file).
 *   - cchan needs pthreads; under JIT they resolve from the host `classyc`
 *     binary (no -l pthread). Put any real -l/-L/-I flags *before* -eg.
 *   - -w quiets false-positive ownership warnings inside cchan.h's free paths
 *     when the ClassyC analyzer walks the included implementation.
 *
 * @expect: exit 0 and print "ALL CHANNEL SMOKES PASSED"
 */

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

/* ── tiny harness ─────────────────────────────────────────────────────── */

static int g_failed = 0;

static void expect(int cond, const char *msg) {
    if (cond) {
        printf("  ok  %s\n", msg);
    } else {
        g_failed++;
        printf("  FAIL %s\n", msg);
    }
}

/* ── 1. unbuffered rendezvous ─────────────────────────────────────────── */

typedef struct {
    cchan_t *ch;
    int      value;
} Rendezvous;

void *rendezvous_sender(void *arg) {
    Rendezvous *r = (Rendezvous *)arg;
    cchan_send(r->ch, &r->value);
    return 0;
}

void demo_unbuffered(void) {
    printf("-- unbuffered rendezvous\n");

    cchan_t *ch = cchan_create(0, sizeof(int));
    expect(ch != 0, "cchan_create(0, sizeof(int))");
    expect(cchan_capacity(ch) == 0, "capacity is 0 (unbuffered)");

    Rendezvous r;
    r.ch = ch;
    r.value = 42;

    pthread_t t;
    pthread_create(&t, 0, rendezvous_sender, &r);

    int out = 0;
    int ok = cchan_recv(ch, &out);
    pthread_join(t, 0);

    expect(ok == 1 && out == 42, "recv rendezvous value 42");
    cchan_dispose(ch);
}

/* ── 2. buffered producer / consumer with close ───────────────────────── */

typedef struct {
    cchan_t *ch;
    int      from;
    int      to;
} RangeJob;

void *range_producer(void *arg) {
    RangeJob *job = (RangeJob *)arg;
    for (int i = job->from; i <= job->to; i++) {
        if (!cchan_send_i32(job->ch, (int32_t)i)) {
            printf("  producer: send failed at %d (closed?)\n", i);
            break;
        }
    }
    /* signal EOF — consumer drains then sees recv fail */
    cchan_close(job->ch);
    return 0;
}

void demo_buffered_close(void) {
    printf("-- buffered producer → consumer (close + drain)\n");

    cchan_t *ch = cchan_create(4, sizeof(int32_t));
    expect(ch != 0, "cchan_create(4, sizeof(int32_t))");
    expect(cchan_capacity(ch) == 4, "capacity is 4");

    RangeJob job;
    job.ch = ch;
    job.from = 1;
    job.to = 10;

    pthread_t t;
    pthread_create(&t, 0, range_producer, &job);

    int32_t v = 0;
    int sum = 0;
    int n = 0;
    while (cchan_recv_i32(ch, &v)) {
        sum += (int)v;
        n++;
        printf("  recv %d\n", (int)v);
    }
    pthread_join(t, 0);

    expect(n == 10 && sum == 55, "drained 1..10 (n=10 sum=55)");
    expect(cchan_is_closed(ch) == 1, "channel is closed after drain");
    cchan_dispose(ch);
}

/* ── 3. fan-in: several workers → one channel ─────────────────────────── */

typedef struct {
    cchan_t *ch;
    int      id;
    int      n;
} WorkerJob;

void *fanin_worker(void *arg) {
    WorkerJob *w = (WorkerJob *)arg;
    for (int i = 0; i < w->n; i++) {
        /* payload: high byte = worker id, low = sequence */
        int32_t msg = (int32_t)((w->id << 16) | (i & 0xffff));
        cchan_send_i32(w->ch, msg);
    }
    return 0;
}

void demo_fanin(void) {
    printf("-- fan-in (3 workers × 5 msgs)\n");

    enum { NW = 3, MSGS = 5 };
    cchan_t *ch = cchan_create(8, sizeof(int32_t));
    WorkerJob jobs[NW];
    pthread_t ts[NW];

    for (int i = 0; i < NW; i++) {
        jobs[i].ch = ch;
        jobs[i].id = i + 1;
        jobs[i].n = MSGS;
        pthread_create(&ts[i], 0, fanin_worker, &jobs[i]);
    }

    int counts[NW + 1];
    for (int i = 0; i <= NW; i++) counts[i] = 0;

    int total = NW * MSGS;
    for (int k = 0; k < total; k++) {
        int32_t msg = 0;
        if (!cchan_recv_i32(ch, &msg)) {
            expect(0, "recv all fan-in messages");
            break;
        }
        int wid = (int)(msg >> 16);
        if (wid >= 1 && wid <= NW) counts[wid]++;
    }

    for (int i = 0; i < NW; i++)
        pthread_join(ts[i], 0);

    int ok = 1;
    for (int i = 1; i <= NW; i++) {
        if (counts[i] != MSGS) ok = 0;
        printf("  worker %d delivered %d/%d\n", i, counts[i], MSGS);
    }
    expect(ok, "each worker delivered all messages");

    cchan_close(ch);
    cchan_dispose(ch);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    printf("classy-cchan: CSP channels via ext/ccchan\n\n");

    demo_unbuffered();
    printf("\n");
    demo_buffered_close();
    printf("\n");
    demo_fanin();

    printf("\n");
    if (g_failed == 0) {
        printf("ALL CHANNEL SMOKES PASSED\n");
        return 0;
    }
    printf("%d check(s) FAILED\n", g_failed);
    return 1;
}
