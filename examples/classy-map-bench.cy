/* classy-map-bench.cy — throughput benchmark for include/map.h
 *
 * Exercises Map<K, V> at scale (default 100,000 entries) and reports timings
 * for insert, lookup (hit), lookup (miss / Contains), for-in iteration, and
 * remove — for both int keys and String keys.  Each phase also verifies a
 * checksum, so this doubles as a heavy correctness test.
 *
 * Usage:   classyc examples/classy-map-bench.cy -eg
 * Tune N:  classyc -DBENCH_N=1000000 examples/classy-map-bench.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "include/map.h"

#ifndef BENCH_N
#define BENCH_N 100000
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* Millions of ops per second (guards against a zero-duration phase). */
static double mops(long n, double ms) {
    return ms > 0.0 ? (double)n / ms / 1000.0 : 0.0;
}

int failures = 0;
void expect(int cond, const char* what) {
    if (!cond) { printf("  !! CHECK FAILED: %s\n", what); failures++; }
}

int main() {
    long N = BENCH_N;
    printf("=== Map<K,V> benchmark  (N = %ld) ===\n", N);

    /* ───────────────────────── Map<int,int> ───────────────────────── */

    /* Pre-size to N so we time steady-state ops, not incremental rehashing. */
    Map<int, int>* mi = new Map<int, int>((int)N);
    defer delete mi;

    double t0 = now_ms();
    for (int i = 0; i < N; i++) mi->Set(i, i * 3 + 1);
    double ins = now_ms() - t0;
    expect(mi->Count() == (int)N, "int insert count");

    long long sum_hit = 0;
    t0 = now_ms();
    for (int i = 0; i < N; i++) sum_hit += mi->Get(i);
    double get = now_ms() - t0;

    int hits = 0;
    t0 = now_ms();
    for (int i = (int)N; i < 2 * N; i++) if (mi->Contains(i)) hits++;
    double miss = now_ms() - t0;
    expect(hits == 0, "int miss lookups find nothing");

    long long sum_iter = 0;
    t0 = now_ms();
    for (auto k, v in mi) sum_iter += v;
    double iter = now_ms() - t0;
    expect(sum_iter == sum_hit, "int for-in sum == Get sum");

    t0 = now_ms();
    for (int i = 0; i < N; i++) mi->Remove(i);
    double del = now_ms() - t0;
    expect(mi->Count() == 0, "int remove empties map");

    printf("\nMap<int,int>\n");
    printf("  insert   : %9.2f ms   %6.2f M ops/s\n", ins,  mops(N, ins));
    printf("  lookup   : %9.2f ms   %6.2f M ops/s\n", get,  mops(N, get));
    printf("  miss     : %9.2f ms   %6.2f M ops/s\n", miss, mops(N, miss));
    printf("  for-in   : %9.2f ms   %6.2f M ops/s\n", iter, mops(N, iter));
    printf("  remove   : %9.2f ms   %6.2f M ops/s\n", del,  mops(N, del));

    /* ──────────────────────── Map<String,int> ─────────────────────── */

    /* Pre-materialize N distinct String keys so the timed phases measure map
     * work, not key construction.  detach() is required: an f-string built in a
     * loop lives in the per-iteration loop arena, which is reclaimed at the end
     * of each iteration — so without detach the stored pointers would alias
     * reclaimed slots (garbage contents, key collisions).  detach escapes each
     * key to the heap so it survives in the array; we free them at the end. */
    String* keys = (String*) malloc(sizeof(String) * N);
    for (int i = 0; i < N; i++) keys[i] = (f"key_{i}").detach();

    Map<String, int>* ms = new Map<String, int>((int)N);
    defer delete ms;

    t0 = now_ms();
    for (int i = 0; i < N; i++) ms->Set(keys[i], i);
    double sins = now_ms() - t0;
    expect(ms->Count() == (int)N, "string insert count");

    long long ssum = 0;
    t0 = now_ms();
    for (int i = 0; i < N; i++) ssum += ms->Get(keys[i]);
    double sget = now_ms() - t0;
    expect(ssum == (long long)(N - 1) * N / 2, "string Get checksum");

    long long siter = 0;
    t0 = now_ms();
    for (auto k, v in ms) siter += v;
    double siter_ms = now_ms() - t0;
    expect(siter == ssum, "string for-in sum == Get sum");

    t0 = now_ms();
    for (int i = 0; i < N; i++) ms->Remove(keys[i]);
    double sdel = now_ms() - t0;
    expect(ms->Count() == 0, "string remove empties map");

    printf("\nMap<String,int>\n");
    printf("  insert   : %9.2f ms   %6.2f M ops/s\n", sins,     mops(N, sins));
    printf("  lookup   : %9.2f ms   %6.2f M ops/s\n", sget,     mops(N, sget));
    printf("  for-in   : %9.2f ms   %6.2f M ops/s\n", siter_ms, mops(N, siter_ms));
    printf("  remove   : %9.2f ms   %6.2f M ops/s\n", sdel,     mops(N, sdel));

    /* keys were detach()'d to the heap, so free each one, then the array. */
    for (int i = 0; i < N; i++) free((void*)keys[i]);
    free((void*)keys);

    if (failures == 0) printf("\n=== all checks passed ===\n");
    else               printf("\n=== %d CHECK(S) FAILED ===\n", failures);
    return failures == 0 ? 0 : 1;
}
