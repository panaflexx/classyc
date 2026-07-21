/*
 * stress_crazy.c — how far can cchan go with OS threads?
 *
 *   gcc -O2 -Wall -Wextra -pthread -o stress_crazy stress_crazy.c
 *   ./stress_crazy              # default ladder
 *   ./stress_crazy --quick      # shorter
 *   ./stress_crazy --insane     # go big
 *   ./stress_crazy --bench      # throughput numbers only
 *
 * Exercises:
 *   1) Thread spawn ceiling (how many joinable threads we can create)
 *   2) Unbuffered rendezvous storm (pair match thrashing)
 *   3) Buffered MPMC correctness + throughput ladder
 *   4) Select fan-in storm (many chans, many selectors)
 *   5) Pipeline depth (stage0 → stage1 → … → stageN)
 *   6) Close races under full load
 *   7) Try + budget/timeout hot paths mixed with peers
 *   8) Ping-pong matrix (unbuffered pairs)
 *   9) Worker-pool hammer
 *  10) Dedicated budget / timeout / select_timeout pressure
 *
 * Throughput benches use blocking send/recv/select (safe topology + close).
 * Budget/timeout APIs are hammered in scenarios 7 and 10 (and in
 * stress_trading for multi-hop anti-deadlock patterns).
 *
 * Exit 0 if all scenarios that ran passed; non-zero on correctness failure.
 */

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/resource.h>

/* -------------------------------------------------------------------------- */
/* utilities                                                                  */
/* -------------------------------------------------------------------------- */

static int g_fail = 0;
static int g_quick = 0;
static int g_insane = 0;
static int g_bench = 0;

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

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int spawn(pthread_t* t, void* (*fn)(void*), void* arg)
{
    pthread_attr_t attr;
    int rc;
    /* Smaller stacks => more threads before address-space exhaustion. */
    if (pthread_attr_init(&attr) != 0)
        return 0;
    pthread_attr_setstacksize(&attr, 64 * 1024); /* 64 KiB */
    rc = pthread_create(t, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc == 0;
}

static int spawn_default(pthread_t* t, void* (*fn)(void*), void* arg)
{
    return pthread_create(t, 0, fn, arg) == 0;
}

/*
 * Performance policy for this harness:
 *
 *   Microbenches with safe topology (pair rendezvous, MPMC + close drain,
 *   linear pipeline with concurrent sink) use blocking cchan_send/recv/select.
 *   Timeouts wake via the kernel clock and cost ~tens of µs per wait — fine for
 *   apps, terrible for transfer-rate benches.
 *
 *   Budget / timeout APIs are still exercised hard in scenario_try_hot and
 *   scenario_budget_timeout (and used correctly in stress_trading).
 */

/* -------------------------------------------------------------------------- */
/* 1. Thread ceiling                                                          */
/* -------------------------------------------------------------------------- */

static void* noop_thread(void* arg)
{
    /* Park until closed — cchan_close wakes all waiters. */
    cchan_t* go = (cchan_t*)arg;
    int v;
    cchan_recv(go, &v);
    return 0;
}

static void scenario_thread_ceiling(void)
{
    /* Find how many concurrent threads we can hold, parked on a channel. */
    enum { MAX_TRY = 50000 };
    int target = g_insane ? 20000 : (g_quick ? 500 : 4000);
    if (target > MAX_TRY) target = MAX_TRY;

    pthread_t* tids = (pthread_t*)calloc((size_t)target, sizeof(pthread_t));
    cchan_t* go = cchan_create(0, sizeof(int));
    int created = 0;
    int i;
    double t0, t1;

    HEAD("thread ceiling (parked on unbuffered recv), target=%d", target);
    t0 = now_s();
    for (i = 0; i < target; i++) {
        cchan_retain(go);
        if (!spawn(&tids[i], noop_thread, go)) {
            cchan_release(go);
            INFO("pthread_create failed at %d: %s", i, strerror(errno));
            break;
        }
        created++;
        if ((created % 1000) == 0)
            INFO("created %d ...", created);
    }
    t1 = now_s();
    INFO("created %d threads in %.3fs (%.0f threads/s)",
         created, t1 - t0, created / (t1 - t0 + 1e-9));

    /* Wake everyone by closing — recv returns 0. */
    t0 = now_s();
    cchan_close(go);
    for (i = 0; i < created; i++)
        pthread_join(tids[i], 0);
    t1 = now_s();
    INFO("joined %d in %.3fs", created, t1 - t0);

    /* drop retains from successful spawns + initial create */
    for (i = 0; i < created; i++)
        cchan_release(go);
    cchan_dispose(go);
    free(tids);
    INFO("thread ceiling result: %d concurrent", created);
}

/* -------------------------------------------------------------------------- */
/* 2. Unbuffered rendezvous storm                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    int iters;
    atomic_ullong* ops;
} ren_arg;

static void* ren_sender(void* arg)
{
    ren_arg* a = (ren_arg*)arg;
    int i, v = 1;
    for (i = 0; i < a->iters; i++) {
        if (!cchan_send(a->c, &v))
            break;
        atomic_fetch_add(a->ops, 1);
    }
    return 0;
}

static void* ren_receiver(void* arg)
{
    ren_arg* a = (ren_arg*)arg;
    int i, v;
    for (i = 0; i < a->iters; i++) {
        if (!cchan_recv(a->c, &v))
            break;
        atomic_fetch_add(a->ops, 1);
    }
    return 0;
}

static void scenario_rendezvous_storm(void)
{
    int pairs = g_insane ? 256 : (g_quick ? 16 : 64);
    int iters = g_insane ? 20000 : (g_quick ? 2000 : 10000);
    int i;
    double t0, t1;
    atomic_ullong ops = 0;
    pthread_t* tids = (pthread_t*)calloc((size_t)pairs * 2, sizeof(pthread_t));
    ren_arg* args = (ren_arg*)calloc((size_t)pairs * 2, sizeof(ren_arg));
    cchan_t** chans = (cchan_t**)calloc((size_t)pairs, sizeof(cchan_t*));

    HEAD("unbuffered rendezvous storm: %d pairs × %d iters", pairs, iters);

    for (i = 0; i < pairs; i++) {
        chans[i] = cchan_create(0, sizeof(int));
        args[2*i].c = chans[i];
        args[2*i].iters = iters;
        args[2*i].ops = &ops;
        args[2*i+1].c = chans[i];
        args[2*i+1].iters = iters;
        args[2*i+1].ops = &ops;
    }

    t0 = now_s();
    for (i = 0; i < pairs; i++) {
        if (!spawn_default(&tids[2*i], ren_sender, &args[2*i]))
            FAIL("spawn sender %d", i);
        if (!spawn_default(&tids[2*i+1], ren_receiver, &args[2*i+1]))
            FAIL("spawn receiver %d", i);
    }
    for (i = 0; i < pairs * 2; i++)
        pthread_join(tids[i], 0);
    t1 = now_s();

    {
        unsigned long long expect = (unsigned long long)pairs * (unsigned long long)iters * 2ull;
        unsigned long long got = atomic_load(&ops);
        double secs = t1 - t0;
        /* each transfer = 1 send + 1 recv counted = 2 ops */
        INFO("ops=%llu expect=%llu  time=%.3fs  transfers/s=%.0f",
             (unsigned long long)got, expect, secs,
             ((double)pairs * iters) / (secs + 1e-9));
        if (got != expect)
            FAIL("rendezvous ops mismatch got=%llu expect=%llu",
                 (unsigned long long)got, expect);
    }

    for (i = 0; i < pairs; i++)
        cchan_dispose(chans[i]);
    free(chans); free(args); free(tids);
}

/* -------------------------------------------------------------------------- */
/* 3. MPMC ladder                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    int id;
    int n_send;
    atomic_ullong* sum_sent;
    atomic_ullong* sum_recv;
    atomic_ullong* n_recv;
} mpmc_arg;

static void* mpmc_prod(void* arg)
{
    mpmc_arg* a = (mpmc_arg*)arg;
    int i;
    unsigned long long local = 0;
    for (i = 0; i < a->n_send; i++) {
        int v = a->id * a->n_send + i;
        if (!cchan_send(a->c, &v))
            break;
        local += (unsigned)v;
    }
    atomic_fetch_add(a->sum_sent, local);
    return 0;
}

static void* mpmc_cons(void* arg)
{
    mpmc_arg* a = (mpmc_arg*)arg;
    int v;
    unsigned long long local = 0, cnt = 0;
    /* Blocking drain-until-close — correct and fast once producers join+close. */
    while (cchan_recv(a->c, &v)) {
        local += (unsigned)v;
        cnt++;
    }
    atomic_fetch_add(a->sum_recv, local);
    atomic_fetch_add(a->n_recv, cnt);
    return 0;
}

static int run_mpmc(int nprod, int ncons, int per_prod, int cap, double* out_rate)
{
    pthread_t* tids = (pthread_t*)calloc((size_t)(nprod + ncons), sizeof(pthread_t));
    mpmc_arg* args = (mpmc_arg*)calloc((size_t)(nprod + ncons), sizeof(mpmc_arg));
    cchan_t* c = cchan_create((unsigned short)cap, sizeof(int));
    atomic_ullong sum_sent = 0, sum_recv = 0, n_recv = 0;
    int i, ok = 1;
    double t0, t1;
    unsigned long long expect_sum = 0;
    unsigned long long total_msgs = (unsigned long long)nprod * (unsigned long long)per_prod;

    for (i = 0; i < nprod; i++) {
        int base = i * per_prod;
        int j;
        for (j = 0; j < per_prod; j++)
            expect_sum += (unsigned)(base + j);
    }

    t0 = now_s();
    for (i = 0; i < ncons; i++) {
        args[nprod + i].c = c;
        args[nprod + i].sum_recv = &sum_recv;
        args[nprod + i].n_recv = &n_recv;
        if (!spawn_default(&tids[nprod + i], mpmc_cons, &args[nprod + i])) {
            FAIL("mpmc cons spawn");
            ok = 0;
        }
    }
    for (i = 0; i < nprod; i++) {
        args[i].c = c;
        args[i].id = i;
        args[i].n_send = per_prod;
        args[i].sum_sent = &sum_sent;
        if (!spawn_default(&tids[i], mpmc_prod, &args[i])) {
            FAIL("mpmc prod spawn");
            ok = 0;
        }
    }
    for (i = 0; i < nprod; i++)
        pthread_join(tids[i], 0);
    cchan_close(c);
    for (i = 0; i < ncons; i++)
        pthread_join(tids[nprod + i], 0);
    t1 = now_s();

    {
        unsigned long long ss = atomic_load(&sum_sent);
        unsigned long long sr = atomic_load(&sum_recv);
        unsigned long long nr = atomic_load(&n_recv);
        double secs = t1 - t0;
        double rate = (double)total_msgs / (secs + 1e-9);
        if (out_rate) *out_rate = rate;

        INFO("P=%d C=%d cap=%d msgs=%llu  %.3fs  %.2f M/s  sum_sent=%llu sum_recv=%llu",
             nprod, ncons, cap, total_msgs, secs, rate / 1e6, ss, sr);

        if (ss != expect_sum) {
            FAIL("sum_sent mismatch %llu vs %llu", ss, expect_sum);
            ok = 0;
        }
        if (sr != expect_sum) {
            FAIL("sum_recv mismatch %llu vs %llu", sr, expect_sum);
            ok = 0;
        }
        if (nr != total_msgs) {
            FAIL("n_recv mismatch %llu vs %llu", nr, total_msgs);
            ok = 0;
        }
    }

    cchan_dispose(c);
    free(args);
    free(tids);
    return ok;
}

typedef struct { int p, c, per, cap; } mpmc_step;

static void scenario_mpmc_ladder(void)
{
    static const mpmc_step steps_default[] = {
        { 1, 1, 100000, 64 },
        { 2, 2, 100000, 64 },
        { 4, 4, 100000, 128 },
        { 8, 8, 50000, 256 },
        { 16, 16, 25000, 256 },
        { 32, 32, 10000, 512 },
        { 64, 64, 5000, 1024 },
        { 128, 128, 2000, 1024 },
        { 0, 0, 0, 0 }
    };
    static const mpmc_step steps_quick[] = {
        { 4, 4, 20000, 64 },
        { 16, 16, 5000, 128 },
        { 0, 0, 0, 0 }
    };
    static const mpmc_step steps_insane[] = {
        { 8, 8, 200000, 256 },
        { 32, 32, 100000, 512 },
        { 64, 64, 50000, 1024 },
        { 128, 128, 20000, 2048 },
        { 256, 256, 5000, 4096 },
        { 0, 0, 0, 0 }
    };
    const mpmc_step *steps = steps_default;
    int i;

    if (g_quick) steps = steps_quick;
    if (g_insane) steps = steps_insane;

    HEAD("MPMC correctness + throughput ladder");
    for (i = 0; steps[i].p; i++) {
        double rate = 0;
        if (!run_mpmc(steps[i].p, steps[i].c, steps[i].per, steps[i].cap, &rate))
            FAIL("mpmc step P=%d C=%d failed", steps[i].p, steps[i].c);
    }

    /* Unbuffered mpmc (harder). Keep smaller than buffered ladder — rendezvous
       under 32×32 is correctness, not a throughput showcase. */
    {
        int p = g_insane ? 32 : (g_quick ? 8 : 16);
        int per = g_insane ? 10000 : (g_quick ? 2000 : 4000);
        double rate = 0;
        INFO("unbuffered MPMC P=%d C=%d per=%d", p, p, per);
        if (!run_mpmc(p, p, per, 0, &rate))
            FAIL("unbuffered mpmc failed");
    }
}

/* -------------------------------------------------------------------------- */
/* 4. Select fan-in storm                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t** chans;
    int nchans;
    int id;
    int n_send;
    atomic_ullong* sent;
} fan_prod_arg;

typedef struct {
    cchan_t** chans;
    int nchans;
    atomic_ullong* got;
    atomic_ullong* sum;
    atomic_int* stop;
} fan_sel_arg;

/* Real payloads always have high bit set so closed-EOF zeros are not counted. */
#define FAN_TAG 0x80000000u

static void* fan_prod(void* arg)
{
    fan_prod_arg* a = (fan_prod_arg*)arg;
    int i;
    for (i = 0; i < a->n_send; i++) {
        int ch = (a->id + i) % a->nchans;
        int v = (int)(FAN_TAG | (unsigned)((a->id << 16) ^ i));
        if (!cchan_send(a->chans[ch], &v))
            break;
        atomic_fetch_add(a->sent, 1);
    }
    return 0;
}

static void* fan_selector(void* arg)
{
    fan_sel_arg* a = (fan_sel_arg*)arg;
    /*
     * Live set of channels. Closed empty chans stay forever-ready under Go
     * select semantics (zero value), so we MUST drop them or we spin forever.
     * Blocking select is correct here: producers run, then channels are closed.
     */
    cchan_t** live = (cchan_t**)malloc(sizeof(cchan_t*) * (size_t)a->nchans);
    cchan_t** recvs = (cchan_t**)malloc(sizeof(cchan_t*) * (size_t)a->nchans);
    void** rdata = (void**)malloc(sizeof(void*) * (size_t)a->nchans);
    int* vals = (int*)malloc(sizeof(int) * (size_t)a->nchans);
    int nlive = a->nchans;
    int i;
    unsigned long long local_n = 0, local_sum = 0;

    if (!live || !recvs || !rdata || !vals) {
        free(live); free(recvs); free(rdata); free(vals);
        return 0;
    }
    for (i = 0; i < a->nchans; i++)
        live[i] = a->chans[i];

    while (nlive > 0) {
        int idx;
        for (i = 0; i < nlive; i++) {
            recvs[i] = live[i];
            vals[i] = 0;
            rdata[i] = &vals[i];
        }
        idx = cchan_select(recvs, rdata, (unsigned)nlive, 0, 0, 0);
        if (idx < 0)
            break;

        if (((unsigned)vals[idx] & FAN_TAG) != 0) {
            local_n++;
            local_sum += (unsigned)vals[idx];
        } else if (cchan_is_closed(live[idx]) && cchan_size(live[idx]) == 0) {
            live[idx] = live[nlive - 1];
            nlive--;
        }
        (void)atomic_load(a->stop);
    }

    atomic_fetch_add(a->got, local_n);
    atomic_fetch_add(a->sum, local_sum);
    free(live); free(recvs); free(rdata); free(vals);
    return 0;
}

static void scenario_select_fanin(void)
{
    int nchans = g_insane ? 64 : (g_quick ? 8 : 32);
    int nprod  = g_insane ? 128 : (g_quick ? 16 : 64);
    int nsel   = g_insane ? 32 : (g_quick ? 4 : 16);
    int per    = g_insane ? 5000 : (g_quick ? 500 : 2000);
    int i;
    cchan_t** chans = (cchan_t**)calloc((size_t)nchans, sizeof(cchan_t*));
    pthread_t* pt = (pthread_t*)calloc((size_t)nprod, sizeof(pthread_t));
    pthread_t* st = (pthread_t*)calloc((size_t)nsel, sizeof(pthread_t));
    fan_prod_arg* pa = (fan_prod_arg*)calloc((size_t)nprod, sizeof(fan_prod_arg));
    fan_sel_arg* sa = (fan_sel_arg*)calloc((size_t)nsel, sizeof(fan_sel_arg));
    atomic_ullong sent = 0, got = 0, sum = 0;
    atomic_int stop = 0;
    double t0, t1;
    unsigned long long expect = (unsigned long long)nprod * (unsigned long long)per;

    HEAD("select fan-in: chans=%d prod=%d sel=%d per=%d (expect msgs=%llu)",
         nchans, nprod, nsel, per, expect);

    for (i = 0; i < nchans; i++)
        chans[i] = cchan_create(32, sizeof(int));

    for (i = 0; i < nsel; i++) {
        sa[i].chans = chans;
        sa[i].nchans = nchans;
        sa[i].got = &got;
        sa[i].sum = &sum;
        sa[i].stop = &stop;
        if (!spawn_default(&st[i], fan_selector, &sa[i]))
            FAIL("selector spawn");
    }
    t0 = now_s();
    for (i = 0; i < nprod; i++) {
        pa[i].chans = chans;
        pa[i].nchans = nchans;
        pa[i].id = i;
        pa[i].n_send = per;
        pa[i].sent = &sent;
        if (!spawn_default(&pt[i], fan_prod, &pa[i]))
            FAIL("prod spawn");
    }
    for (i = 0; i < nprod; i++)
        pthread_join(pt[i], 0);

    /* close all channels so selectors exit */
    for (i = 0; i < nchans; i++)
        cchan_close(chans[i]);
    atomic_store(&stop, 1);
    for (i = 0; i < nsel; i++)
        pthread_join(st[i], 0);
    t1 = now_s();

    {
        unsigned long long s = atomic_load(&sent);
        unsigned long long g = atomic_load(&got);
        double secs = t1 - t0;
        INFO("sent=%llu got=%llu time=%.3fs rate=%.2f Mselect-recv/s",
             s, g, secs, (double)g / (secs + 1e-9) / 1e6);
        if (s != expect)
            FAIL("select fan-in sent mismatch %llu vs %llu", s, expect);
        if (g != expect)
            FAIL("select fan-in got mismatch %llu vs %llu", g, expect);
    }

    for (i = 0; i < nchans; i++)
        cchan_dispose(chans[i]);
    free(chans); free(pt); free(st); free(pa); free(sa);
}

/* -------------------------------------------------------------------------- */
/* 5. Pipeline depth                                                          */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* in;
    cchan_t* out;
    int add;
} pipe_arg;

static void* pipe_stage(void* arg)
{
    pipe_arg* p = (pipe_arg*)arg;
    int v;
    /*
     * Linear pipeline + concurrent source/sink is a safe topology for blocking
     * send/recv (each stage's out is another stage's in with active drain).
     * Budget/timeout would only add timer noise here.
     */
    while (cchan_recv(p->in, &v)) {
        v += p->add;
        if (!cchan_send(p->out, &v))
            break;
    }
    cchan_close(p->out);
    return 0;
}

typedef struct {
    cchan_t* out;
    int nmsg;
    int stages;
    long long* expect;
} pipe_src_arg;

static void* pipe_source(void* arg)
{
    pipe_src_arg* s = (pipe_src_arg*)arg;
    int i, v;
    long long exp = 0;
    for (i = 0; i < s->nmsg; i++) {
        v = i;
        if (!cchan_send(s->out, &v))
            break;
        exp += (long long)i + s->stages;
    }
    *s->expect = exp;
    cchan_close(s->out);
    return 0;
}

static void scenario_pipeline(void)
{
    int stages = g_insane ? 256 : (g_quick ? 16 : 64);
    int nmsg = g_insane ? 50000 : (g_quick ? 5000 : 20000);
    int i, v;
    cchan_t** ch = (cchan_t**)calloc((size_t)(stages + 1), sizeof(cchan_t*));
    pthread_t* th = (pthread_t*)calloc((size_t)stages + 1, sizeof(pthread_t));
    pipe_arg* pa = (pipe_arg*)calloc((size_t)stages, sizeof(pipe_arg));
    pipe_src_arg src;
    long long checksum = 0, expect = 0;
    double t0, t1;

    HEAD("pipeline: %d stages × %d msgs", stages, nmsg);

    for (i = 0; i <= stages; i++)
        ch[i] = cchan_create(16, sizeof(int));

    for (i = 0; i < stages; i++) {
        pa[i].in = ch[i];
        pa[i].out = ch[i + 1];
        pa[i].add = 1; /* each stage +1 */
        if (!spawn_default(&th[i], pipe_stage, &pa[i]))
            FAIL("pipe stage spawn %d", i);
    }

    /* Source runs concurrently with sink so bounded channels cannot deadlock. */
    src.out = ch[0];
    src.nmsg = nmsg;
    src.stages = stages;
    src.expect = &expect;
    t0 = now_s();
    if (!spawn_default(&th[stages], pipe_source, &src))
        FAIL("pipe source spawn");

    for (i = 0; i < nmsg; i++) {
        if (!cchan_recv(ch[stages], &v)) {
            FAIL("pipe recv early EOF at %d", i);
            break;
        }
        checksum += v;
    }
    /* drain EOF */
    if (cchan_recv(ch[stages], &v))
        FAIL("pipe expected EOF");

    for (i = 0; i <= stages; i++)
        pthread_join(th[i], 0);
    t1 = now_s();

    INFO("time=%.3fs thruput=%.2f Mmsg/s through %d stages (%.2f Mstage-ops/s)",
         t1 - t0,
         (double)nmsg / (t1 - t0 + 1e-9) / 1e6,
         stages,
         (double)nmsg * stages / (t1 - t0 + 1e-9) / 1e6);
    if (checksum != expect)
        FAIL("pipeline checksum %lld vs %lld", checksum, expect);

    for (i = 0; i <= stages; i++)
        cchan_dispose(ch[i]);
    free(ch); free(th); free(pa);
}

/* -------------------------------------------------------------------------- */
/* 6. Close races under load                                                  */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    atomic_ullong* ops;
    atomic_int* done;
} race_arg;

static void* race_sender(void* arg)
{
    race_arg* a = (race_arg*)arg;
    int v = 1;
    while (!atomic_load(a->done)) {
        /* try first (racy close path), fall back to short budget under full */
        int rc = cchan_try_send(a->c, &v);
        if (rc == 0)
            rc = cchan_send_budget(a->c, &v, 32);
        if (rc == 1)
            atomic_fetch_add(a->ops, 1);
        else if (rc < 0)
            break;
    }
    return 0;
}

static void* race_receiver(void* arg)
{
    race_arg* a = (race_arg*)arg;
    int v;
    while (!atomic_load(a->done) || cchan_size(a->c) > 0) {
        int rc = cchan_try_recv(a->c, &v);
        if (rc == 0)
            rc = cchan_recv_budget(a->c, &v, 32);
        if (rc == 1)
            atomic_fetch_add(a->ops, 1);
        else if (rc < 0)
            break;
    }
    return 0;
}

static void scenario_close_race(void)
{
    int rounds = g_insane ? 200 : (g_quick ? 20 : 80);
    int nsend = g_insane ? 32 : (g_quick ? 4 : 16);
    int nrecv = nsend;
    int r, i;
    unsigned long long total_ops = 0;

    HEAD("close races: %d rounds × %d senders × %d recvs", rounds, nsend, nrecv);

    for (r = 0; r < rounds; r++) {
        cchan_t* c = cchan_create(64, sizeof(int));
        pthread_t* t = (pthread_t*)calloc((size_t)(nsend + nrecv), sizeof(pthread_t));
        race_arg* a = (race_arg*)calloc((size_t)(nsend + nrecv), sizeof(race_arg));
        atomic_ullong ops = 0;
        atomic_int done = 0;

        for (i = 0; i < nsend; i++) {
            a[i].c = c; a[i].ops = &ops; a[i].done = &done;
            spawn_default(&t[i], race_sender, &a[i]);
        }
        for (i = 0; i < nrecv; i++) {
            a[nsend + i].c = c; a[nsend + i].ops = &ops; a[nsend + i].done = &done;
            spawn_default(&t[nsend + i], race_receiver, &a[nsend + i]);
        }

        /* let them thrash */
        cchan_sleep(g_quick ? 5 : 15);
        cchan_close(c);
        atomic_store(&done, 1);

        for (i = 0; i < nsend + nrecv; i++)
            pthread_join(t[i], 0);

        total_ops += atomic_load(&ops);
        cchan_dispose(c);
        free(t); free(a);
    }
    INFO("survived %d close-race rounds, total ops~%llu", rounds, total_ops);
}

/* -------------------------------------------------------------------------- */
/* 7. Hot try_ path mixed with blocking                                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    int iters;
    atomic_ullong* hits;
} try_arg;

static void* try_spinner(void* arg)
{
    try_arg* a = (try_arg*)arg;
    int i, v = 0, out = 0;
    for (i = 0; i < a->iters; i++) {
        /* mix pure try and short budget (same family of APIs) */
        int rc;
        if ((i & 7) == 0)
            rc = cchan_send_budget(a->c, &v, 4);
        else
            rc = cchan_try_send(a->c, &v);
        if (rc == 1) atomic_fetch_add(a->hits, 1);

        if ((i & 7) == 3)
            rc = cchan_recv_budget(a->c, &out, 4);
        else
            rc = cchan_try_recv(a->c, &out);
        if (rc == 1) atomic_fetch_add(a->hits, 1);

        if ((i & 63) == 0)
            v++;
    }
    return 0;
}

static void* try_blocker_send(void* arg)
{
    try_arg* a = (try_arg*)arg;
    int i, v = 1000000;
    for (i = 0; i < a->iters / 10; i++) {
        if (cchan_send_timeout(a->c, &v, 50) != 1) {
            if (cchan_is_closed(a->c)) break;
            continue;
        }
        atomic_fetch_add(a->hits, 1);
        v++;
    }
    return 0;
}

static void* try_blocker_recv(void* arg)
{
    try_arg* a = (try_arg*)arg;
    int i, v;
    for (i = 0; i < a->iters / 10; i++) {
        if (cchan_recv_timeout(a->c, &v, 50) != 1) {
            if (cchan_is_closed(a->c) && cchan_size(a->c) == 0) break;
            continue;
        }
        atomic_fetch_add(a->hits, 1);
    }
    return 0;
}

static void scenario_try_hot(void)
{
    int nspin = g_insane ? 64 : (g_quick ? 8 : 24);
    int iters = g_insane ? 200000 : (g_quick ? 20000 : 100000);
    cchan_t* c = cchan_create(8, sizeof(int));
    pthread_t* t = (pthread_t*)calloc((size_t)(nspin + 2), sizeof(pthread_t));
    try_arg* a = (try_arg*)calloc((size_t)(nspin + 2), sizeof(try_arg));
    atomic_ullong hits = 0;
    int i;
    double t0, t1;

    HEAD("try/budget/timeout hot spin: %d spinners × %d iters + 2 blockers", nspin, iters);

    for (i = 0; i < nspin; i++) {
        a[i].c = c; a[i].iters = iters; a[i].hits = &hits;
        spawn_default(&t[i], try_spinner, &a[i]);
    }
    a[nspin].c = c; a[nspin].iters = iters; a[nspin].hits = &hits;
    a[nspin+1].c = c; a[nspin+1].iters = iters; a[nspin+1].hits = &hits;
    spawn_default(&t[nspin], try_blocker_send, &a[nspin]);
    spawn_default(&t[nspin+1], try_blocker_recv, &a[nspin+1]);

    t0 = now_s();
    for (i = 0; i < nspin + 2; i++)
        pthread_join(t[i], 0);
    t1 = now_s();

    cchan_close(c);
    /* drain */
    {
        int v;
        while (cchan_try_recv(c, &v) == 1)
            atomic_fetch_add(&hits, 1);
    }

    INFO("hits=%llu time=%.3fs  try-ops attempt rate ~ %.2f M/s",
         (unsigned long long)atomic_load(&hits), t1 - t0,
         ((double)nspin * iters * 2.0) / (t1 - t0 + 1e-9) / 1e6);

    cchan_dispose(c);
    free(t); free(a);
}

/* -------------------------------------------------------------------------- */
/* 8. Ping-pong matrix (many unbuffered pairs, tight loop)                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* a;
    cchan_t* b;
    int rounds;
    atomic_ullong* ops;
} pp_arg;

static void* pp_left(void* arg)
{
    pp_arg* p = (pp_arg*)arg;
    int i, v = 0;
    for (i = 0; i < p->rounds; i++) {
        if (!cchan_send(p->a, &v)) break;
        if (!cchan_recv(p->b, &v)) break;
        atomic_fetch_add(p->ops, 1);
    }
    return 0;
}

static void* pp_right(void* arg)
{
    pp_arg* p = (pp_arg*)arg;
    int i, v = 0;
    for (i = 0; i < p->rounds; i++) {
        if (!cchan_recv(p->a, &v)) break;
        v++;
        if (!cchan_send(p->b, &v)) break;
        atomic_fetch_add(p->ops, 1);
    }
    return 0;
}

static void scenario_pingpong_matrix(void)
{
    int pairs = g_insane ? 512 : (g_quick ? 32 : 128);
    int rounds = g_insane ? 20000 : (g_quick ? 2000 : 10000);
    int i;
    pthread_t* t = (pthread_t*)calloc((size_t)pairs * 2, sizeof(pthread_t));
    pp_arg* a = (pp_arg*)calloc((size_t)pairs, sizeof(pp_arg));
    cchan_t** ch = (cchan_t**)calloc((size_t)pairs * 2, sizeof(cchan_t*));
    atomic_ullong ops = 0;
    double t0, t1;

    HEAD("ping-pong matrix: %d pairs × %d rounds (unbuffered)", pairs, rounds);

    for (i = 0; i < pairs; i++) {
        ch[2*i] = cchan_create(0, sizeof(int));
        ch[2*i+1] = cchan_create(0, sizeof(int));
        a[i].a = ch[2*i];
        a[i].b = ch[2*i+1];
        a[i].rounds = rounds;
        a[i].ops = &ops;
    }

    t0 = now_s();
    for (i = 0; i < pairs; i++) {
        spawn_default(&t[2*i], pp_left, &a[i]);
        spawn_default(&t[2*i+1], pp_right, &a[i]);
    }
    for (i = 0; i < pairs * 2; i++)
        pthread_join(t[i], 0);
    t1 = now_s();

    {
        unsigned long long o = atomic_load(&ops);
        /* each round each side counts 1 => 2*pairs*rounds */
        unsigned long long expect = 2ull * (unsigned long long)pairs * (unsigned long long)rounds;
        double secs = t1 - t0;
        INFO("ops=%llu expect=%llu time=%.3fs round-trips/s=%.0f",
             o, expect, secs, ((double)pairs * rounds) / (secs + 1e-9));
        if (o != expect)
            FAIL("pingpong ops %llu vs %llu", o, expect);
    }

    for (i = 0; i < pairs * 2; i++)
        cchan_dispose(ch[i]);
    free(t); free(a); free(ch);
}

/* -------------------------------------------------------------------------- */
/* 9. Hammer: everything at once                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* work;
    cchan_t* done;
    int id;
    int n;
    atomic_ullong* ok;
} hammer_arg;

static void* hammer_worker(void* arg)
{
    hammer_arg* h = (hammer_arg*)arg;
    int i;
    for (i = 0; i < h->n; i++) {
        int job = 0;
        if (!cchan_recv(h->work, &job))
            break;
        /* tiny CPU */
        job = job * 1664525 + 1013904223;
        /* Concurrent sink keeps `done` draining — blocking send is fine. */
        if (!cchan_send(h->done, &job))
            break;
        atomic_fetch_add(h->ok, 1);
    }
    return 0;
}

typedef struct {
    cchan_t* done;
    int jobs;
    atomic_ullong* received;
} hammer_sink_arg;

static void* hammer_sink(void* arg)
{
    hammer_sink_arg* s = (hammer_sink_arg*)arg;
    int i, v;
    for (i = 0; i < s->jobs; i++) {
        if (!cchan_recv(s->done, &v))
            break;
        atomic_fetch_add(s->received, 1);
    }
    return 0;
}

static void scenario_hammer(void)
{
    int workers = g_insane ? 256 : (g_quick ? 16 : 64);
    int jobs = g_insane ? 500000 : (g_quick ? 20000 : 200000);
    int i, v;
    cchan_t* work = cchan_create(1024, sizeof(int));
    cchan_t* done = cchan_create(1024, sizeof(int));
    pthread_t* t = (pthread_t*)calloc((size_t)workers + 1, sizeof(pthread_t));
    hammer_arg* a = (hammer_arg*)calloc((size_t)workers, sizeof(hammer_arg));
    hammer_sink_arg sink;
    atomic_ullong ok = 0, received = 0;
    double t0, t1;

    HEAD("worker pool hammer: %d workers, %d jobs", workers, jobs);

    for (i = 0; i < workers; i++) {
        a[i].work = work;
        a[i].done = done;
        a[i].id = i;
        a[i].n = jobs; /* upper bound; exit early on work EOF */
        a[i].ok = &ok;
        spawn_default(&t[i], hammer_worker, &a[i]);
    }

    /* Concurrent sink prevents done-buffer deadlock while source enqueues. */
    sink.done = done;
    sink.jobs = jobs;
    sink.received = &received;
    spawn_default(&t[workers], hammer_sink, &sink);

    t0 = now_s();
    for (i = 0; i < jobs; i++) {
        v = i;
        if (!cchan_send(work, &v))
            FAIL("hammer enqueue");
    }
    cchan_close(work);

    pthread_join(t[workers], 0);
    cchan_close(done);
    for (i = 0; i < workers; i++)
        pthread_join(t[i], 0);
    t1 = now_s();

    {
        unsigned long long r = atomic_load(&received);
        INFO("completed %llu/%d jobs in %.3fs (%.2f Mjobs/s) worker-ops=%llu",
             r, jobs, t1 - t0,
             (double)r / (t1 - t0 + 1e-9) / 1e6,
             (unsigned long long)atomic_load(&ok));
        if (r != (unsigned long long)jobs)
            FAIL("hammer incomplete");
    }

    cchan_dispose(work);
    cchan_dispose(done);
    free(t); free(a);
}

/* -------------------------------------------------------------------------- */
/* 10. Budget / timeout / select_timeout dedicated pressure                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    atomic_ullong* hits;
    atomic_int* stop;
} bt_arg;

static void* bt_producer(void* arg)
{
    bt_arg* a = (bt_arg*)arg;
    int v = 1;
    while (!atomic_load(a->stop)) {
        int rc = cchan_send_budget(a->c, &v, 32);
        if (rc == 1) {
            atomic_fetch_add(a->hits, 1);
            v++;
        } else if (rc < 0) {
            break;
        }
    }
    return 0;
}

static void* bt_consumer(void* arg)
{
    bt_arg* a = (bt_arg*)arg;
    int v;
    while (!atomic_load(a->stop) || cchan_size(a->c) > 0) {
        int rc = cchan_recv_budget(a->c, &v, 32);
        if (rc == 1) {
            atomic_fetch_add(a->hits, 1);
        } else if (rc < 0) {
            break;
        } else if (atomic_load(a->stop) && cchan_size(a->c) == 0) {
            break;
        }
    }
    return 0;
}

typedef struct {
    cchan_t* c;
    int val;
    unsigned delay_ms;
} late_send_arg;

static void* late_send_thread(void* arg)
{
    late_send_arg* a = (late_send_arg*)arg;
    cchan_sleep(a->delay_ms);
    (void)cchan_send_timeout(a->c, &a->val, 200);
    return 0;
}

static void scenario_budget_timeout(void)
{
    cchan_t* c;
    cchan_t* recvs[2];
    void* rbufs[2];
    int vals[2];
    int idx, v;
    double t0, t1;
    atomic_ullong hits = 0;
    atomic_int stop = 0;
    pthread_t tp, tc, late;
    bt_arg pa, ca;
    late_send_arg la;
    int duration_ms = g_insane ? 800 : (g_quick ? 80 : 300);

    HEAD("budget/timeout/select_timeout pressure (%dms)", duration_ms);

    /* 1) pure timeout on empty/full */
    c = cchan_create(1, sizeof(int));
    t0 = now_s();
    {
        int rc = cchan_recv_timeout(c, &v, 40);
        if (rc != 0) FAIL("recv_timeout empty expected 0 got %d", rc);
        v = 1;
        if (cchan_send_budget(c, &v, 0) != 1) FAIL("seed send");
        v = 2;
        rc = cchan_send_timeout(c, &v, 40);
        if (rc != 0) FAIL("send_timeout full expected 0 got %d", rc);
        t1 = now_s();
        if ((t1 - t0) * 1000.0 < 30.0)
            FAIL("timeouts returned too fast (%.1fms)", (t1 - t0) * 1000.0);
    }
    cchan_dispose(c);

    /* 2) budget returns would-block when full, closed when closed */
    c = cchan_create(1, sizeof(int));
    v = 9;
    if (cchan_send_budget(c, &v, 0) != 1) FAIL("send_budget empty");
    v = 10;
    if (cchan_send_budget(c, &v, 16) != 0) FAIL("send_budget full");
    cchan_close(c);
    if (cchan_send_budget(c, &v, 4) != -1) FAIL("send_budget closed");
    if (cchan_recv_budget(c, &v, 4) != 1 || v != 9) FAIL("recv_budget drain");
    if (cchan_recv_budget(c, &v, 4) != -1) FAIL("recv_budget EOF");
    cchan_dispose(c);

    /* 3) select_timeout: timeout vs data */
    c = cchan_create(2, sizeof(int));
    recvs[0] = c;
    rbufs[0] = &vals[0];
    vals[0] = 0;
    idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 30);
    if (idx != -1) FAIL("select_timeout open-empty expected -1 got %d", idx);
    v = 42;
    if (cchan_send_budget(c, &v, 0) != 1) FAIL("select seed");
    idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 100);
    if (idx != 0 || vals[0] != 42)
        FAIL("select_timeout data idx=%d val=%d", idx, vals[0]);
    cchan_close(c);
    idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 50);
    /* closed empty: ready EOF (0) or all-closed (-2) depending on poll path */
    if (idx != 0 && idx != -2)
        FAIL("select_timeout closed got %d", idx);
    cchan_dispose(c);

    /* 4) concurrent budget producers/consumers for a short wall window */
    c = cchan_create(32, sizeof(int));
    pa.c = c; pa.hits = &hits; pa.stop = &stop;
    ca.c = c; ca.hits = &hits; ca.stop = &stop;
    spawn_default(&tp, bt_producer, &pa);
    spawn_default(&tc, bt_consumer, &ca);
    cchan_sleep((unsigned)duration_ms);
    atomic_store(&stop, 1);
    cchan_close(c);
    pthread_join(tp, 0);
    pthread_join(tc, 0);
    INFO("budget MPMC hits=%llu over %dms",
         (unsigned long long)atomic_load(&hits), duration_ms);
    if (atomic_load(&hits) == 0)
        FAIL("budget MPMC made no progress");
    cchan_dispose(c);

    /* 5) select_timeout wakes when a late sender posts */
    {
        cchan_t* a = cchan_create(1, sizeof(int));
        cchan_t* b = cchan_create(1, sizeof(int));
        recvs[0] = a;
        recvs[1] = b;
        rbufs[0] = &vals[0];
        rbufs[1] = &vals[1];
        vals[0] = vals[1] = 0;
        la.c = b;
        la.val = 77;
        la.delay_ms = 25;
        spawn_default(&late, late_send_thread, &la);
        idx = cchan_select_timeout(recvs, rbufs, 2, 0, 0, 0, 500);
        pthread_join(late, 0);
        if (idx != 1 || vals[1] != 77)
            FAIL("late select_timeout idx=%d val=%d", idx, vals[1]);
        cchan_dispose(a);
        cchan_dispose(b);
    }

    INFO("budget/timeout checks completed");
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

static void print_limits(void)
{
    struct rlimit rl;
    HEAD("system snapshot");
    INFO("cores (nproc-ish): %ld", sysconf(_SC_NPROCESSORS_ONLN));
    if (getrlimit(RLIMIT_NPROC, &rl) == 0)
        INFO("RLIMIT_NPROC soft=%lu hard=%lu",
             (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    if (getrlimit(RLIMIT_STACK, &rl) == 0)
        INFO("RLIMIT_STACK soft=%lu", (unsigned long)rl.rlim_cur);
    if (getrlimit(RLIMIT_AS, &rl) == 0)
        INFO("RLIMIT_AS soft=%s",
             rl.rlim_cur == RLIM_INFINITY ? "inf" : "set");
    INFO("mode: %s", g_insane ? "INSANE" : (g_quick ? "quick" : "default"));
}

int main(int argc, char** argv)
{
    int i;
    double wall0, wall1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) g_quick = 1;
        else if (strcmp(argv[i], "--insane") == 0) g_insane = 1;
        else if (strcmp(argv[i], "--bench") == 0) g_bench = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                "usage: %s [--quick|--insane] [--bench]\n"
                "  --quick   smaller thread/msg counts\n"
                "  --insane  push hard (many threads, many msgs)\n"
                "  --bench   skip ceiling probe (throughput focus)\n",
                argv[0]);
            return 0;
        }
    }

    srand((unsigned)time(0) ^ (unsigned)getpid());
    printf("cchan CRAZY thread stress\n");
    print_limits();

    wall0 = now_s();

    if (!g_bench)
        scenario_thread_ceiling();

    scenario_rendezvous_storm();
    scenario_mpmc_ladder();
    scenario_select_fanin();
    scenario_pipeline();
    scenario_close_race();
    scenario_try_hot();
    scenario_pingpong_matrix();
    scenario_hammer();
    scenario_budget_timeout();

    wall1 = now_s();
    printf("\n========================================\n");
    printf("wall time: %.2fs\n", wall1 - wall0);
    if (g_fail == 0) {
        printf("CRAZY STRESS: ALL PASSED\n");
        return 0;
    }
    printf("CRAZY STRESS: %d FAILURES\n", g_fail);
    return 1;
}
