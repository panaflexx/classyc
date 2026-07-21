/*
 * cchan self-test suite
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o test test.c -lpthread
 *
 * Sanitizers:
 *   gcc -O1 -g -fsanitize=address,undefined -o test test.c -lpthread
 *   gcc -O1 -g -fsanitize=thread            -o test test.c -lpthread
 *
 * Expect: "ALL TESTS PASSED" and exit 0.
 */

#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/* harness                                                                    */
/* -------------------------------------------------------------------------- */

static int g_failed = 0;
static int g_passed = 0;

static void t_check(int cond, const char* file, int line, const char* expr)
{
    if (cond) {
        g_passed++;
    } else {
        g_failed++;
        fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
        fflush(stderr);
    }
}

#define CHECK(expr) t_check(!!(expr), __FILE__, __LINE__, #expr)

static void section(const char* name)
{
    printf("-- %s\n", name);
    fflush(stdout);
}

static int spawn(pthread_t* t, void* (*fn)(void*), void* arg)
{
    return pthread_create(t, 0, fn, arg) == 0;
}

/* -------------------------------------------------------------------------- */
/* 1. buffered basics + size/capacity                                         */
/* -------------------------------------------------------------------------- */

static void test_buffered_basic(void)
{
    int v;
    cchan_t* c;
    section("buffered basic");

    c = cchan_create(2, sizeof(int));
    CHECK(c != 0);
    CHECK(cchan_capacity(c) == 2);
    CHECK(cchan_msg_size(c) == (int)sizeof(int));
    CHECK(cchan_size(c) == 0);
    CHECK(cchan_is_closed(c) == 0);

    v = 10; CHECK(cchan_send(c, &v) == 1);
    CHECK(cchan_size(c) == 1);
    v = 20; CHECK(cchan_send(c, &v) == 1);
    CHECK(cchan_size(c) == 2);
    v = 30; CHECK(cchan_try_send(c, &v) == 0); /* full */

    CHECK(cchan_recv(c, &v) == 1 && v == 10);
    CHECK(cchan_recv(c, &v) == 1 && v == 20);
    CHECK(cchan_try_recv(c, &v) == 0);
    CHECK(cchan_size(c) == 0);

    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 2. unbuffered rendezvous                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    int value;
    int ok;
} pair_arg;

static void* pair_sender(void* arg)
{
    pair_arg* a = (pair_arg*)arg;
    a->ok = cchan_send(a->c, &a->value);
    return 0;
}

static void* pair_receiver(void* arg)
{
    pair_arg* a = (pair_arg*)arg;
    int got = -1;
    a->ok = cchan_recv(a->c, &got);
    a->value = got;
    return 0;
}

static void test_unbuffered_rendezvous(void)
{
    pthread_t ts, tr;
    pair_arg sa, ra;
    cchan_t* c;
    section("unbuffered rendezvous");

    c = cchan_create(0, sizeof(int));
    CHECK(cchan_capacity(c) == 0);
    CHECK(cchan_size(c) == 0);

    sa.c = c; sa.value = 42; sa.ok = 0;
    ra.c = c; ra.value = -1; ra.ok = 0;
    CHECK(spawn(&tr, pair_receiver, &ra));
    cchan_sleep(20);
    CHECK(spawn(&ts, pair_sender, &sa));
    pthread_join(ts, 0);
    pthread_join(tr, 0);
    CHECK(sa.ok == 1 && ra.ok == 1 && ra.value == 42);
    cchan_dispose(c);

    /* sender first */
    c = cchan_create(0, sizeof(int));
    sa.c = c; sa.value = 99; sa.ok = 0;
    ra.c = c; ra.value = -1; ra.ok = 0;
    CHECK(spawn(&ts, pair_sender, &sa));
    cchan_sleep(20);
    CHECK(spawn(&tr, pair_receiver, &ra));
    pthread_join(ts, 0);
    pthread_join(tr, 0);
    CHECK(sa.ok == 1 && ra.ok == 1 && ra.value == 99);
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 3. close semantics                                                         */
/* -------------------------------------------------------------------------- */

static void* blocked_recv(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    int v = 123;
    int ok = cchan_recv(c, &v);
    return (void*)(intptr_t)((ok << 16) | (v & 0xffff));
}

static void* blocked_send(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    int v = 7;
    return (void*)(intptr_t)cchan_send(c, &v);
}

static void test_close_semantics(void)
{
    cchan_t* c;
    int v;
    pthread_t t;
    void* ret;
    section("close semantics");

    c = cchan_create(2, sizeof(int));
    CHECK(cchan_close(c) == 0);
    CHECK(cchan_close(c) == -1); /* already closed */
    CHECK(cchan_is_closed(c) == 1);
    v = 99;
    CHECK(cchan_recv(c, &v) == 0);
    CHECK(v == 0);
    /* second recv must not hang (historical mutex-leak bug) */
    CHECK(cchan_recv(c, &v) == 0);
    CHECK(cchan_send(c, &v) == 0);
    CHECK(cchan_try_send(c, &v) == -1);
    CHECK(cchan_try_recv(c, &v) == -1);
    cchan_dispose(c);

    /* drain then EOF */
    c = cchan_create(2, sizeof(int));
    v = 1; cchan_send(c, &v);
    v = 2; cchan_send(c, &v);
    cchan_close(c);
    CHECK(cchan_recv(c, &v) == 1 && v == 1);
    CHECK(cchan_recv(c, &v) == 1 && v == 2);
    CHECK(cchan_recv(c, &v) == 0);
    cchan_dispose(c);

    /* close wakes blocked receiver */
    c = cchan_create(0, sizeof(int));
    CHECK(spawn(&t, blocked_recv, c));
    cchan_sleep(50);
    cchan_close(c);
    pthread_join(t, &ret);
    CHECK(((intptr_t)ret >> 16) == 0);
    cchan_dispose(c);

    /* close wakes blocked sender on full buffer */
    c = cchan_create(1, sizeof(int));
    v = 1; cchan_send(c, &v);
    CHECK(spawn(&t, blocked_send, c));
    cchan_sleep(50);
    cchan_close(c);
    pthread_join(t, &ret);
    CHECK((intptr_t)ret == 0);
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 4. refcount                                                                */
/* -------------------------------------------------------------------------- */

static void test_refcount(void)
{
    cchan_t* c;
    int v;
    section("refcount");

    c = cchan_create(1, sizeof(int));
    cchan_retain(c);
    cchan_retain(c);
    v = 5; CHECK(cchan_send(c, &v) == 1);
    cchan_release(c);
    cchan_release(c);
    CHECK(cchan_recv(c, &v) == 1 && v == 5);
    cchan_release(c);
}

/* -------------------------------------------------------------------------- */
/* 5. mpmc                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* c;
    int from, to;
    int sum_out;
    pthread_mutex_t* sum_mu;
} work_arg;

static void* producer(void* arg)
{
    work_arg* w = (work_arg*)arg;
    int i;
    for (i = w->from; i < w->to; i++) {
        if (!cchan_send(w->c, &i))
            break;
    }
    return 0;
}

static void* consumer(void* arg)
{
    work_arg* w = (work_arg*)arg;
    int v, local = 0;
    while (cchan_recv(w->c, &v))
        local += v;
    pthread_mutex_lock(w->sum_mu);
    w->sum_out += local;
    pthread_mutex_unlock(w->sum_mu);
    return 0;
}

static void test_mpmc(void)
{
    enum { N_PROD = 4, N_CONS = 4, N_EACH = 500 };
    pthread_t pt[N_PROD], ct[N_CONS];
    work_arg pa[N_PROD], ca[N_CONS];
    pthread_mutex_t sum_mu = PTHREAD_MUTEX_INITIALIZER;
    cchan_t* c;
    int i, total = 0, expected = 0;
    section("mpmc buffered");

    c = cchan_create(32, sizeof(int));
    for (i = 0; i < N_PROD * N_EACH; i++)
        expected += i;

    for (i = 0; i < N_CONS; i++) {
        ca[i].c = c;
        ca[i].sum_out = 0;
        ca[i].sum_mu = &sum_mu;
        CHECK(spawn(&ct[i], consumer, &ca[i]));
    }
    for (i = 0; i < N_PROD; i++) {
        pa[i].c = c;
        pa[i].from = i * N_EACH;
        pa[i].to = (i + 1) * N_EACH;
        CHECK(spawn(&pt[i], producer, &pa[i]));
    }
    for (i = 0; i < N_PROD; i++)
        pthread_join(pt[i], 0);

    cchan_close(c);
    for (i = 0; i < N_CONS; i++) {
        pthread_join(ct[i], 0);
        total += ca[i].sum_out;
    }
    CHECK(total == expected);
    cchan_dispose(c);
    pthread_mutex_destroy(&sum_mu);
}

/* -------------------------------------------------------------------------- */
/* 6. select multi-recv (with sentinel close)                                 */
/* -------------------------------------------------------------------------- */

/*
 * Simple protocol: send N values then a negative sentinel, then the sender
 * exits. Receiver selects until it has seen a sentinel on every channel.
 * No dependency on closed-channel select EOF races.
 */
typedef struct {
    cchan_t* c;
    int start, n, step;
} seq_arg;

static void* seq_sender(void* arg)
{
    seq_arg* s = (seq_arg*)arg;
    int i, v;
    for (i = 0; i < s->n; i++) {
        v = s->start + i * s->step;
        if (!cchan_send(s->c, &v))
            break;
    }
    v = -1; /* sentinel */
    cchan_send(s->c, &v);
    return 0;
}

static void test_select_recv(void)
{
    cchan_t *a, *b;
    pthread_t ta, tb;
    seq_arg sa, sb;
    int got_a = 0, got_b = 0;
    int done_a = 0, done_b = 0;
    section("select multi-recv");

    a = cchan_create(0, sizeof(int));
    b = cchan_create(4, sizeof(int));
    sa.c = a; sa.start = 0; sa.n = 50; sa.step = 2;
    sb.c = b; sb.start = 1; sb.n = 50; sb.step = 2;
    CHECK(spawn(&ta, seq_sender, &sa));
    CHECK(spawn(&tb, seq_sender, &sb));

    while (!done_a || !done_b) {
        int x = 0, y = 0;
        cchan_t* recvs[2];
        void* rdata[2];
        unsigned n = 0;
        int idx;

        if (!done_a) { recvs[n] = a; rdata[n] = &x; n++; }
        if (!done_b) { recvs[n] = b; rdata[n] = &y; n++; }

        idx = cchan_select(recvs, rdata, n, 0, 0, 0);
        if (idx < 0) {
            /* Should not happen while senders still use open channels. */
            CHECK(idx >= 0);
            break;
        }

        if (recvs[idx] == a) {
            if (x < 0) {
                done_a = 1;
            } else {
                CHECK((x % 2) == 0);
                got_a++;
            }
        } else {
            if (y < 0) {
                done_b = 1;
            } else {
                CHECK((y % 2) == 1);
                got_b++;
            }
        }
    }

    pthread_join(ta, 0);
    pthread_join(tb, 0);
    CHECK(got_a == 50);
    CHECK(got_b == 50);
    cchan_dispose(a);
    cchan_dispose(b);
}

/* -------------------------------------------------------------------------- */
/* 7. nb_select                                                               */
/* -------------------------------------------------------------------------- */

static void test_nb_select(void)
{
    cchan_t* c;
    int v = 0, out = 0;
    cchan_t* recvs[1];
    cchan_t* sends[1];
    void* rdata[1];
    void* sdata[1];
    section("nb_select");

    c = cchan_create(1, sizeof(int));
    recvs[0] = c; rdata[0] = &out;
    sends[0] = c; sdata[0] = &v;

    v = 11;
    CHECK(cchan_nb_select(0, 0, 0, sends, sdata, 1) == 0);
    CHECK(cchan_size(c) == 1);
    CHECK(cchan_nb_select(recvs, rdata, 1, 0, 0, 0) == 0);
    CHECK(out == 11);
    CHECK(cchan_nb_select(recvs, rdata, 1, 0, 0, 0) == -1);

    cchan_close(c);
    CHECK(cchan_nb_select(recvs, rdata, 1, 0, 0, 0) == 0); /* closed empty */
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 8. select send cases                                                       */
/* -------------------------------------------------------------------------- */

static void* slow_recv(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    int v = 0;
    cchan_sleep(30);
    cchan_recv(c, &v);
    return (void*)(intptr_t)v;
}

static void test_select_send(void)
{
    cchan_t *a, *b;
    pthread_t ta, tb;
    int va = 100, vb = 200;
    int idx;
    void *ra, *rb;
    cchan_t* sends[2];
    void* sdata[2];
    section("select multi-send");

    a = cchan_create(0, sizeof(int));
    b = cchan_create(0, sizeof(int));
    CHECK(spawn(&ta, slow_recv, a));
    CHECK(spawn(&tb, slow_recv, b));

    sends[0] = a; sdata[0] = &va;
    sends[1] = b; sdata[1] = &vb;
    idx = cchan_select(0, 0, 0, sends, sdata, 2);
    CHECK(idx == 0 || idx == 1);

    if (idx == 0)
        CHECK(cchan_send(b, &vb) == 1);
    else
        CHECK(cchan_send(a, &va) == 1);

    pthread_join(ta, &ra);
    pthread_join(tb, &rb);
    CHECK((intptr_t)ra == 100);
    CHECK((intptr_t)rb == 200);
    cchan_dispose(a);
    cchan_dispose(b);
}

/* -------------------------------------------------------------------------- */
/* 9. sieve pipeline                                                          */
/* -------------------------------------------------------------------------- */

typedef struct sieve_node {
    cchan_t* in;
    cchan_t* primes;
} sieve_node;

static void* sieve_filter(void* arg)
{
    sieve_node* n = (sieve_node*)arg;
    int prime = 0, x = 0, eof = -1;
    cchan_t* succ = 0;
    pthread_t child = 0;
    sieve_node* child_arg = 0;
    int has_child = 0;

    if (!cchan_recv(n->in, &prime) || prime < 0) {
        cchan_send(n->primes, &eof);
        free(n);
        return 0;
    }
    cchan_send(n->primes, &prime);

    while (cchan_recv(n->in, &x) && x >= 0) {
        if ((x % prime) != 0) {
            if (!has_child) {
                succ = cchan_create(0, sizeof(int));
                child_arg = (sieve_node*)malloc(sizeof(*child_arg));
                child_arg->in = succ;
                child_arg->primes = n->primes;
                cchan_retain(n->primes);
                if (!spawn(&child, sieve_filter, child_arg)) {
                    cchan_dispose(succ);
                    cchan_release(n->primes);
                    free(child_arg);
                    break;
                }
                has_child = 1;
            }
            cchan_send(succ, &x);
        }
    }

    if (has_child) {
        cchan_send(succ, &eof);
        pthread_join(child, 0);
        cchan_dispose(succ);
    } else {
        cchan_send(n->primes, &eof);
    }

    cchan_release(n->primes);
    free(n);
    return 0;
}

static void* gen_odds(void* arg)
{
    cchan_t* out = (cchan_t*)arg;
    int i, n = 200, v;
    v = 2;
    cchan_send(out, &v);
    for (i = 3; i <= n; i += 2) {
        if (!cchan_send(out, &i))
            break;
    }
    v = -1;
    cchan_send(out, &v);
    return 0;
}

static void test_sieve(void)
{
    static const int expect[] = {
        2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,
        101,103,107,109,113,127,131,137,139,149,151,157,163,167,173,179,181,191,
        193,197,199
    };
    enum { NEXP = (int)(sizeof(expect) / sizeof(expect[0])) };
    int got[NEXP + 8];
    int ngot = 0, v, i;
    cchan_t *nums, *primes;
    pthread_t gen, root;
    sieve_node* root_arg;
    section("sieve pipeline");

    nums = cchan_create(0, sizeof(int));
    primes = cchan_create(8, sizeof(int));

    root_arg = (sieve_node*)malloc(sizeof(*root_arg));
    root_arg->in = nums;
    root_arg->primes = primes;
    cchan_retain(primes);

    CHECK(spawn(&gen, gen_odds, nums));
    CHECK(spawn(&root, sieve_filter, root_arg));

    while (cchan_recv(primes, &v) && v >= 0) {
        if (ngot < (int)(sizeof(got) / sizeof(got[0])))
            got[ngot++] = v;
    }

    pthread_join(gen, 0);
    pthread_join(root, 0);

    CHECK(ngot == NEXP);
    for (i = 0; i < NEXP && i < ngot; i++)
        CHECK(got[i] == expect[i]);

    cchan_dispose(nums);
    cchan_dispose(primes);
}

/* -------------------------------------------------------------------------- */
/* 10. ping-pong                                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* ping;
    cchan_t* pong;
    int rounds;
} pingpong_arg;

static void* ponger(void* arg)
{
    pingpong_arg* p = (pingpong_arg*)arg;
    int i, v;
    for (i = 0; i < p->rounds; i++) {
        if (!cchan_recv(p->ping, &v)) break;
        v++;
        if (!cchan_send(p->pong, &v)) break;
    }
    return 0;
}

static void test_pingpong(void)
{
    enum { ROUNDS = 2000 };
    cchan_t *ping, *pong;
    pthread_t t;
    pingpong_arg a;
    int i, v;
    section("ping-pong unbuffered");

    ping = cchan_create(0, sizeof(int));
    pong = cchan_create(0, sizeof(int));
    a.ping = ping; a.pong = pong; a.rounds = ROUNDS;
    CHECK(spawn(&t, ponger, &a));
    v = 0;
    for (i = 0; i < ROUNDS; i++) {
        CHECK(cchan_send(ping, &v) == 1);
        CHECK(cchan_recv(pong, &v) == 1);
    }
    CHECK(v == ROUNDS);
    pthread_join(t, 0);
    cchan_dispose(ping);
    cchan_dispose(pong);
}

/* -------------------------------------------------------------------------- */
/* 11. stress select                                                          */
/* -------------------------------------------------------------------------- */

typedef struct {
    cchan_t* chans[4];
    volatile int stop;
} stress_arg;

static void* stress_producer(void* arg)
{
    stress_arg* s = (stress_arg*)arg;
    int n = 0;
    while (!s->stop) {
        int idx = n % 4;
        int v = n;
        if (!cchan_send(s->chans[idx], &v))
            break;
        n++;
    }
    return 0;
}

static void test_stress_select(void)
{
    stress_arg s;
    pthread_t tp;
    int i, received = 0;
    section("stress select");

    s.stop = 0;
    for (i = 0; i < 4; i++)
        s.chans[i] = cchan_create(8, sizeof(int));

    CHECK(spawn(&tp, stress_producer, &s));

    while (received < 1000) {
        int v0 = 0, v1 = 0, v2 = 0, v3 = 0;
        cchan_t* recvs[4] = { s.chans[0], s.chans[1], s.chans[2], s.chans[3] };
        void* rdata[4] = { &v0, &v1, &v2, &v3 };
        int idx = cchan_select(recvs, rdata, 4, 0, 0, 0);
        CHECK(idx >= 0 && idx < 4);
        received++;
    }

    s.stop = 1;
    for (i = 0; i < 4; i++)
        cchan_close(s.chans[i]);
    pthread_join(tp, 0);
    for (i = 0; i < 4; i++)
        cchan_dispose(s.chans[i]);
    CHECK(received == 1000);
}

/* -------------------------------------------------------------------------- */
/* 12. struct messages                                                        */
/* -------------------------------------------------------------------------- */

typedef struct {
    char tag;
    int id;
    double x;
} msg_t;

static void* struct_sender(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    msg_t m;
    int i;
    for (i = 0; i < 100; i++) {
        m.tag = (char)('A' + (i % 26));
        m.id = i;
        m.x = i * 0.5;
        if (!cchan_send(c, &m)) break;
    }
    cchan_close(c);
    return 0;
}

static void test_struct_msgs(void)
{
    cchan_t* c;
    pthread_t t;
    msg_t m;
    int n = 0;
    section("struct messages");

    c = cchan_create(3, (unsigned short)sizeof(msg_t));
    CHECK(spawn(&t, struct_sender, c));
    while (cchan_recv(c, &m)) {
        CHECK(m.id == n);
        CHECK(m.tag == (char)('A' + (n % 26)));
        CHECK(m.x == n * 0.5);
        n++;
    }
    CHECK(n == 100);
    pthread_join(t, 0);
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 13. typed helpers                                                          */
/* -------------------------------------------------------------------------- */

static void test_typed(void)
{
    cchan_t* c;
    int32_t a, b;
    double d;
    char buf[8];
    section("typed helpers");

    c = cchan_create(2, sizeof(int32_t));
    CHECK(cchan_send_i32(c, 42) == 1);
    CHECK(cchan_recv_i32(c, &b) == 1 && b == 42);
    cchan_dispose(c);

    c = cchan_create(1, sizeof(double));
    CHECK(cchan_send_double(c, 3.5) == 1);
    CHECK(cchan_recv_double(c, &d) == 1 && d == 3.5);
    cchan_dispose(c);

    c = cchan_create(1, 8);
    CHECK(cchan_send_buf(c, "hello!!", 8) == 1);
    memset(buf, 0, 8);
    CHECK(cchan_recv_buf(c, buf, 8) == 1);
    CHECK(memcmp(buf, "hello!!", 8) == 0);
    /* wrong length rejected */
    CHECK(cchan_send_buf(c, "xx", 2) == 0);
    cchan_dispose(c);

    /* mismatch type rejected */
    c = cchan_create(1, sizeof(int64_t));
    a = 1;
    CHECK(cchan_send_i32(c, a) == 0);
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* 14. fibonacci tree                                                         */
/* -------------------------------------------------------------------------- */

typedef struct fib_arg {
    cchan_t* out;
    int n;
} fib_arg;

static int fib_ref(int n)
{
    if (n <= 1) return n;
    return fib_ref(n - 1) + fib_ref(n - 2);
}

static void* fib_worker(void* arg)
{
    fib_arg* fa = (fib_arg*)arg;
    int result;
    if (fa->n <= 1) {
        result = fa->n;
        cchan_send(fa->out, &result);
    } else {
        cchan_t* left = cchan_create(1, sizeof(int));
        cchan_t* right = cchan_create(1, sizeof(int));
        fib_arg *a1 = (fib_arg*)malloc(sizeof(fib_arg));
        fib_arg *a2 = (fib_arg*)malloc(sizeof(fib_arg));
        pthread_t t1, t2;
        int x = 0, y = 0;
        a1->out = left;  a1->n = fa->n - 1;
        a2->out = right; a2->n = fa->n - 2;
        spawn(&t1, fib_worker, a1);
        spawn(&t2, fib_worker, a2);
        cchan_recv(left, &x);
        cchan_recv(right, &y);
        pthread_join(t1, 0);
        pthread_join(t2, 0);
        cchan_dispose(left);
        cchan_dispose(right);
        /* a1/a2 freed by child workers */
        result = x + y;
        cchan_send(fa->out, &result);
    }
    free(fa);
    return 0;
}

static void test_fibonacci(void)
{
    const int n = 12;
    cchan_t* out = cchan_create(1, sizeof(int));
    fib_arg* arg = (fib_arg*)malloc(sizeof(fib_arg));
    pthread_t t;
    int got = -1;
    section("fibonacci tree");

    arg->out = out;
    arg->n = n;
    CHECK(spawn(&t, fib_worker, arg));
    CHECK(cchan_recv(out, &got) == 1);
    pthread_join(t, 0);
    CHECK(got == fib_ref(n));
    cchan_dispose(out);
}

/* -------------------------------------------------------------------------- */
/* 15. concurrent close vs recv storm                                         */
/* -------------------------------------------------------------------------- */

static void* storm_recv(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    int v, n = 0;
    while (cchan_recv(c, &v))
        n++;
    return (void*)(intptr_t)n;
}

static void* storm_send(void* arg)
{
    cchan_t* c = (cchan_t*)arg;
    int i;
    for (i = 0; i < 200; i++) {
        if (!cchan_send(c, &i))
            break;
    }
    return 0;
}

static void test_close_storm(void)
{
    enum { N = 8 };
    cchan_t* c = cchan_create(16, sizeof(int));
    pthread_t rt[N], st[N];
    int i;
    intptr_t total = 0;
    section("close storm");

    for (i = 0; i < N; i++) {
        CHECK(spawn(&rt[i], storm_recv, c));
        CHECK(spawn(&st[i], storm_send, c));
    }
    cchan_sleep(20);
    cchan_close(c);
    for (i = 0; i < N; i++) {
        void* r;
        pthread_join(st[i], 0);
        pthread_join(rt[i], &r);
        total += (intptr_t)r;
    }
    /* some messages may have transferred; just ensure we didn't hang/crash */
    CHECK(total >= 0);
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* budget + timeout                                                           */
/* -------------------------------------------------------------------------- */

static void test_budget_and_timeout(void)
{
    cchan_t* c;
    int v, out;
    cchan_t* recvs[1];
    void* rbufs[1];
    int idx;
    double t0, t1;
    section("budget + timeout");

    /* budget: full buffer exhausts without hanging */
    c = cchan_create(1, sizeof(int));
    v = 1; CHECK(cchan_send_budget(c, &v, 0) == 1);
    v = 2; CHECK(cchan_send_budget(c, &v, 64) == 0); /* full */
    CHECK(cchan_recv_budget(c, &out, 0) == 1 && out == 1);
    CHECK(cchan_recv_budget(c, &out, 8) == 0); /* empty */
    cchan_close(c);
    v = 3; CHECK(cchan_send_budget(c, &v, 4) == -1);
    CHECK(cchan_recv_budget(c, &out, 4) == -1);
    cchan_dispose(c);

    /* timeout send on full buffered channel */
    c = cchan_create(1, sizeof(int));
    v = 7; CHECK(cchan_send(c, &v) == 1);
    t0 = (double)clock() / (double)CLOCKS_PER_SEC;
    v = 8; CHECK(cchan_send_timeout(c, &v, 30) == 0);
    t1 = (double)clock() / (double)CLOCKS_PER_SEC;
    (void)t0; (void)t1; /* wall-clock via real time below */
    {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        v = 9; CHECK(cchan_send_timeout(c, &v, 40) == 0);
        clock_gettime(CLOCK_MONOTONIC, &b);
        /* at least ~20ms elapsed */
        {
            double ms = (b.tv_sec - a.tv_sec) * 1000.0 +
                        (b.tv_nsec - a.tv_nsec) / 1e6;
            CHECK(ms >= 20.0);
        }
    }
    CHECK(cchan_recv(c, &out) == 1 && out == 7);
    cchan_dispose(c);

    /* timeout recv on empty */
    c = cchan_create(2, sizeof(int));
    {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        CHECK(cchan_recv_timeout(c, &out, 35) == 0);
        clock_gettime(CLOCK_MONOTONIC, &b);
        {
            double ms = (b.tv_sec - a.tv_sec) * 1000.0 +
                        (b.tv_nsec - a.tv_nsec) / 1e6;
            CHECK(ms >= 15.0);
        }
    }
    /* zero timeout == try */
    CHECK(cchan_recv_timeout(c, &out, 0) == 0);
    v = 11; CHECK(cchan_send_timeout(c, &v, 0) == 1);
    CHECK(cchan_recv_timeout(c, &out, 0) == 1 && out == 11);
    cchan_close(c);
    CHECK(cchan_send_timeout(c, &v, 10) == -1);
    CHECK(cchan_recv_timeout(c, &out, 10) == -1);
    cchan_dispose(c);

    /* select_timeout: timeout vs closed */
    c = cchan_create(1, sizeof(int));
    recvs[0] = c;
    rbufs[0] = &out;
    idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 25);
    CHECK(idx == -1); /* timed out, channel still open */
    cchan_close(c);
    idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 25);
    /* closed empty is ready as EOF case → index 0, or -2 if poll saw all-closed
       without treating closed-recv as ready — we treat closed recv as ready */
    CHECK(idx == 0 || idx == -2);
    cchan_dispose(c);

    /* select_timeout completes when data arrives before deadline */
    c = cchan_create(1, sizeof(int));
    recvs[0] = c;
    rbufs[0] = &out;
    {
        pthread_t t;
        pair_arg sa;
        sa.c = c;
        sa.value = 99;
        sa.ok = 0;
        CHECK(spawn(&t, pair_sender, &sa));
        idx = cchan_select_timeout(recvs, rbufs, 1, 0, 0, 0, 500);
        CHECK(idx == 0 && out == 99);
        pthread_join(t, 0);
    }
    cchan_dispose(c);
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    srand((unsigned)time(0));
    printf("cchan test suite\n");
    fflush(stdout);

    test_buffered_basic();
    test_unbuffered_rendezvous();
    test_close_semantics();
    test_refcount();
    test_mpmc();
    test_select_recv();
    test_nb_select();
    test_select_send();
    test_sieve();
    test_pingpong();
    test_stress_select();
    test_struct_msgs();
    test_typed();
    test_fibonacci();
    test_close_storm();
    test_budget_and_timeout();

    printf("\n%d checks passed, %d failed\n", g_passed, g_failed);
    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        printf("TESTS FAILED\n");
    fflush(stdout);
    return g_failed ? 1 : 0;
}
