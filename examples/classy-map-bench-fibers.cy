/* classy-map-bench-fibers.cy — sharded Map<K,V> benchmark on fibers + channels
 *
 * The fiber/channel port of classy-map-bench.cy.  include/map.h is NOT
 * thread-safe, so instead of locking we shard — the classic Go pattern:
 *
 *        ┌────────────┐   Chan<long> inbox   ┌──────────────────┐
 *        │    main    │ ───────────────────► │ worker fiber w   │
 *        │ (feeder)   │  key index i routed  │  owns Map shard  │
 *        │            │ ◄─────────────────── │  w = i % workers │
 *        └────────────┘   done tokens        └──────────────────┘
 *
 * Every worker fiber owns exactly one Map<K,V> shard; all map ops for a key
 * happen on the owning fiber — zero locks, zero data races.  main routes
 * requests as plain indexes over buffered channels; per-phase results come
 * back in per-worker slots (single writer each) plus a done-token barrier.
 *
 * Phases (same as classy-map-bench.cy, same checksums):
 *   insert / lookup-hit / lookup-miss / for-in / remove   — Map<int,int>
 *   insert / lookup-hit / for-in / remove                 — Map<String,int>
 *
 * Usage:
 *   ./bin/classyc -I include -ffibers examples/classy-map-bench-fibers.cy -eg [--workers=N]
 *   ./classyc-aot.sh -I include -ffibers examples/classy-map-bench-fibers.cy -o /tmp/mapbench
 * Tune N:        -DBENCH_N=1000000
 * Tune workers:  --workers=8   (default 4; 1 = single-thread scheduler)
 */

#include "map.h"
#include "chan.h"
#include <time.h>

#ifndef BENCH_N
#define BENCH_N 100000
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static double mops(long n, double ms) {
    return ms > 0.0 ? (double)n / ms / 1000.0 : 0.0;
}

static int failures = 0;
void expect(int cond, const char* what) {
    if (!cond) { printf("  !! CHECK FAILED: %s\n", what); failures++; }
}

/* ── Sharding state ─────────────────────────────────────────────────────── */
#define MAX_WORKERS 32
#define INBOX_CAP   8192

enum { PH_INSERT, PH_GET, PH_MISS, PH_ITER, PH_REMOVE };

static long      g_n;
static long      g_workers = 4;
static int       g_phase;

static Chan<long>           *g_in[MAX_WORKERS]; /* per-worker request inbox  */
static Chan<long>           *g_done;            /* worker → main barrier     */
static long long             g_results[MAX_WORKERS]; /* per-worker partials  */

static Map<int, int>        *g_imaps[MAX_WORKERS];
static Map<String, int>     *g_smaps[MAX_WORKERS];
static String               *g_skeys;           /* detached key material     */

/* ── Worker: Map<int,int> shard owner ───────────────────────────────────── */
void wint(long w) {
    Map<int, int> *m = g_imaps[w];
    long long acc = 0;

    if (g_phase == PH_ITER) {
        for (auto k, v in m) acc += v;
    } else {
        Chan<long> *in = g_in[w];
        long i = 0;
        while (in->recv(&i)) {          /* parks; false once closed+drained */
            if (g_phase == PH_INSERT)      m->Set((int)i, (int)(i * 3 + 1));
            else if (g_phase == PH_GET)    acc += m->Get((int)i);
            else if (g_phase == PH_MISS)   { if (m->Contains((int)i)) acc++; }
            else if (g_phase == PH_REMOVE) m->Remove((int)i);
        }
        if (g_phase == PH_INSERT || g_phase == PH_REMOVE) acc = m->Count();
    }
    g_results[w] = acc;                 /* single writer per slot — no lock */
    g_done->send(w);
}

/* ── Worker: Map<String,int> shard owner ────────────────────────────────── */
void wstr(long w) {
    Map<String, int> *m = g_smaps[w];
    long long acc = 0;

    if (g_phase == PH_ITER) {
        for (auto k, v in m) acc += v;
    } else {
        Chan<long> *in = g_in[w];
        long i = 0;
        while (in->recv(&i)) {
            String key = g_skeys[i];
            if (g_phase == PH_INSERT)      m->Set(key, (int)i);
            else if (g_phase == PH_GET)    acc += m->Get(key);
            else if (g_phase == PH_REMOVE) m->Remove(key);
        }
        if (g_phase == PH_INSERT || g_phase == PH_REMOVE) acc = m->Count();
    }
    g_results[w] = acc;
    g_done->send(w);
}

/* ── Phase driver: spawn workers, start pool, feed, close, drain, barrier ── */
static double run_phase(int phase, int string_p, int feed, long feed_base) {
    g_phase = phase;
    for (long w = 0; w < g_workers; w++) g_in[w] = new Chan<long>(INBOX_CAP);
    g_done = new Chan<long>((int) g_workers);

    for (long w = 0; w < g_workers; w++) {
        if (string_p) go wstr(w);
        else          go wint(w);
    }

    double t0 = now_ms();
    cy_sched_init((int) g_workers);     /* start the worker pool (non-blocking) */
    if (feed) {
        for (long i = 0; i < g_n; i++)
            g_in[i % g_workers]->send(feed_base + i);  /* route by index → shard */
        for (long w = 0; w < g_workers; w++) g_in[w]->close();
    }
    cy_sched_run();                     /* block until every worker is done */
    double ms = now_ms() - t0;

    long tok = 0;
    for (long w = 0; w < g_workers; w++) g_done->recv(&tok);  /* barrier */
    for (long w = 0; w < g_workers; w++) delete g_in[w];
    delete g_done;
    cy_sched_shutdown();
    return ms;
}

static long long results_sum(void) {
    long long s = 0;
    for (long w = 0; w < g_workers; w++) s += g_results[w];
    return s;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] == '-' && a[1] == '-' && a[2] == 'w') g_workers = atol(a + 10);
    }
    if (g_workers < 1) g_workers = 1;
    if (g_workers > MAX_WORKERS) g_workers = MAX_WORKERS;
    g_n = BENCH_N;

    printf("=== Map<K,V> benchmark on fibers  (N = %ld, workers = %ld) ===\n",
           g_n, g_workers);

    /* Pre-create the shards (main owns construction; workers own the ops). */
    for (long w = 0; w < g_workers; w++)
        g_imaps[w] = new Map<int, int>((int)(g_n / g_workers) + 64);

    /* ───────────────────────── Map<int,int> ───────────────────────── */
    double ins  = run_phase(PH_INSERT, 0, 1, 0);
    expect(results_sum() == g_n, "int insert count");

    double get  = run_phase(PH_GET, 0, 1, 0);
    long long want_hit = 3LL * g_n * (g_n - 1) / 2 + g_n;   /* Σ (3i+1) */
    expect(results_sum() == want_hit, "int lookup checksum");

    double miss = run_phase(PH_MISS, 0, 1, g_n);   /* probe keys N..2N-1 (absent) */
    expect(results_sum() == 0, "int miss lookups find nothing");

    double iter = run_phase(PH_ITER, 0, 0, 0);
    expect(results_sum() == want_hit, "int for-in sum == lookup sum");

    double del  = run_phase(PH_REMOVE, 0, 1, 0);
    expect(results_sum() == 0, "int remove empties shards");

    printf("\nMap<int,int>  (%ld shards)\n", g_workers);
    printf("  insert   : %9.2f ms   %6.2f M ops/s\n", ins,  mops(g_n, ins));
    printf("  lookup   : %9.2f ms   %6.2f M ops/s\n", get,  mops(g_n, get));
    printf("  miss     : %9.2f ms   %6.2f M ops/s\n", miss, mops(g_n, miss));
    printf("  for-in   : %9.2f ms   %6.2f M ops/s\n", iter, mops(g_n, iter));
    printf("  remove   : %9.2f ms   %6.2f M ops/s\n", del,  mops(g_n, del));

    for (long w = 0; w < g_workers; w++) delete g_imaps[w];

    /* ──────────────────────── Map<String,int> ─────────────────────── */

    /* Pre-materialize N distinct detached String keys (same arena rule as
       classy-map-bench.cy: an f-string built in a loop lives in the per-
       iteration arena — detach escapes it to the heap).  Keys ride channels
       as indexes only; the String pointers never cross fibers. */
    g_skeys = (String*) malloc(sizeof(String) * g_n);
    for (long i = 0; i < g_n; i++) g_skeys[i] = (f"key_{i}").detach();

    for (long w = 0; w < g_workers; w++)
        g_smaps[w] = new Map<String, int>((int)(g_n / g_workers) + 64);

    double sins = run_phase(PH_INSERT, 1, 1, 0);
    expect(results_sum() == g_n, "string insert count");

    double sget = run_phase(PH_GET, 1, 1, 0);
    long long swant = g_n * (g_n - 1) / 2;                  /* Σ i */
    expect(results_sum() == swant, "string lookup checksum");

    double siter = run_phase(PH_ITER, 1, 0, 0);
    expect(results_sum() == swant, "string for-in sum == lookup sum");

    double sdel = run_phase(PH_REMOVE, 1, 1, 0);
    expect(results_sum() == 0, "string remove empties shards");

    printf("\nMap<String,int>  (%ld shards)\n", g_workers);
    printf("  insert   : %9.2f ms   %6.2f M ops/s\n", sins,  mops(g_n, sins));
    printf("  lookup   : %9.2f ms   %6.2f M ops/s\n", sget,  mops(g_n, sget));
    printf("  for-in   : %9.2f ms   %6.2f M ops/s\n", siter, mops(g_n, siter));
    printf("  remove   : %9.2f ms   %6.2f M ops/s\n", sdel,  mops(g_n, sdel));

    for (long w = 0; w < g_workers; w++) delete g_smaps[w];
    for (long i = 0; i < g_n; i++) free((void*) g_skeys[i]);
    free((void*) g_skeys);

    if (failures == 0) printf("\n=== all checks passed ===\n");
    else               printf("\n=== %d CHECK(S) FAILED ===\n", failures);
    return failures == 0 ? 0 : 1;
}
