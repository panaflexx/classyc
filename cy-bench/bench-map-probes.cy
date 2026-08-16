/* bench-map-probes.cy — Map<K,V> probe-fusion benchmark (Phase H / R3)
 *
 * Exercises the single-probe rewrites at scale:
 *   · TryAdd with ~50% duplicates            (was 2 probes)
 *   · m[k] = m[k] + 1 presence-style updates (Get+Set, user-side 2 probes)
 *   · GetOrAdd insert-or-read                (was 2 probes on miss)
 *   · GroupBy over a List                    (was 3 probes per element)
 * String keys throughout (FNV-1a content hashing per probe).
 * Checksums verify identical behavior before/after the header change.
 *
 * Usage:  classyc -I include cy-bench/bench-map-probes.cy -eg
 * Tune:   classyc -DBENCH_N=20000 -DBENCH_R=50 -I include … -eg
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "map.h"
#include "list.h"

#ifndef BENCH_N
#define BENCH_N 10000
#endif
#ifndef BENCH_R
#define BENCH_R 30
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

class Rec {
    int id;
    int sector;
    Rec(int id, int sector) { this.id = id; this.sector = sector; }
    ~Rec() {}
    int SectorKey() { return this.sector; }
};

int failures = 0;
void expect(int cond, const char* what) {
    if (!cond) { printf("  !! CHECK FAILED: %s\n", what); failures++; }
}

String key_of(long i) {
    char buf[32];
    snprintf(buf, sizeof buf, "ship-%ld", i);
    return (String)buf;
}

int main() {
    long N = BENCH_N;
    long R = BENCH_R;
    printf("=== Map probe bench  (N=%ld R=%ld) ===\n", N, R);

    /* ── TryAdd: half new, half duplicates ── */
    auto counts = Map<String, int>();
    double t0 = now_ms();
    long inserted = 0;
    for (long i = 0; i < N; i++) {
        if (counts.TryAdd(key_of(i / 2), 1)) inserted++;
    }
    double tryadd = now_ms() - t0;
    expect(inserted == N / 2, "TryAdd inserted count");

    /* ── presence updates: m[k] = m[k] + 1 (GetOr + Set) ── */
    t0 = now_ms();
    for (long f = 0; f < R; f++) {
        for (long i = 0; i < N / 2; i++) {
            String k = key_of(i);
            counts[k] = counts.GetOr(k, 0) + 1;
        }
    }
    double updates = now_ms() - t0;
    long long sum = 0;
    for (auto k, v in counts) sum += v;
    expect(sum == (long long)(N / 2) * (R + 1), "presence sum");

    /* ── GetOrAdd: all hits then all misses ── */
    auto seen = Map<String, int>();
    t0 = now_ms();
    long long acc = 0;
    for (long f = 0; f < R; f++) {
        for (long i = 0; i < N / 2; i++) acc += seen.GetOrAdd(key_of(i), (int)i);
    }
    double getoradd = now_ms() - t0;
    expect(seen.Count() == (int)(N / 2), "GetOrAdd count");
    expect(acc > 0, "GetOrAdd acc");

    /* ── GroupBy over a List (1 probe/elem after fix) ── */
    auto recs = List<Rec>();
    for (long i = 0; i < N; i++) recs.Add(Rec((int)i, (int)(i % 8)));
    t0 = now_ms();
    long buckets = 0, elems = 0;
    for (long f = 0; f < R; f++) {
        auto by = recs.GroupBy((Rec r) => r.SectorKey());
        buckets = by.Count();
        elems = 0;
        for (auto k, v in by) elems += v.Count();
    }
    double groupby = now_ms() - t0;
    expect(buckets == 8 && elems == N, "GroupBy shape");

    printf("tryadd:  %8.2f ms  (%ld ops)\n", tryadd, N);
    printf("updates: %8.2f ms  (%ld get+set pairs)\n", updates, R * N / 2);
    printf("getoradd:%8.2f ms  (%ld ops)\n", getoradd, R * N / 2);
    printf("groupby: %8.2f ms  (%ld x %ld elems)\n", groupby, R, N);
    printf("TOTAL:   %8.2f ms\n", tryadd + updates + getoradd + groupby);
    if (failures > 0) { printf("FAILURES: %d\n", failures); return 1; }
    printf("OK\n");
    return 0;
}
