/* bench-list-pipeline.cy — List<T> pipeline benchmark (Phase H / GEN-OPT-RESEARCH)
 *
 * Mimics the spaceway3k HUD / aurora-ops hot idioms at scale:
 *   · Add-heavy fill loops (capacity fast path)
 *   · Where / Filter over by-value class elements (single-Get rewrite)
 *   · CountWhere scans (per-frame HUD counter pattern)
 *   · Distinct
 * Every phase verifies a checksum — doubles as a correctness test.
 *
 * Usage:  classyc -I include cy-bench/bench-list-pipeline.cy -eg
 * Tune:   classyc -DBENCH_N=2000 -DBENCH_R=200 -I include … -eg
 */

#include <stdio.h>
#include <time.h>
#include "list.h"

#ifndef BENCH_N
#define BENCH_N 1000      /* ships (spaceway3k scale) */
#endif
#ifndef BENCH_R
#define BENCH_R 100       /* frames / repetitions */
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

class Ship {
    int id;
    int faction;
    int heat;
    int cargo;

    Ship(int id, int faction, int heat, int cargo) {
        this.id = id;
        this.faction = faction;
        this.heat = heat;
        this.cargo = cargo;
    }
    ~Ship() {}

    int IsHot()      { return this.heat >= 50; }
    int IsAlive()    { return this.id != 0; }
    int IsTraveling(){ return this.cargo > 0; }
    int IsMerchant() { return this.faction == 2; }
};

int is_hot(Ship s)       { return s.IsHot(); }
int is_alive(Ship s)     { return s.IsAlive(); }
int is_traveling(Ship s) { return s.IsTraveling(); }
int is_merchant(Ship s)  { return s.IsMerchant(); }
int by_heat(Ship a, Ship b) { return a.heat - b.heat; }

int failures = 0;
void expect(int cond, const char* what) {
    if (!cond) { printf("  !! CHECK FAILED: %s\n", what); failures++; }
}

int main() {
    long N = BENCH_N;
    long R = BENCH_R;
    printf("=== List pipeline bench  (N=%ld R=%ld) ===\n", N, R);

    /* ── fill (Add fast path) ── */
    double t0 = now_ms();
    auto fleet = List<Ship>();
    for (long i = 0; i < N; i++)
        fleet.Add(Ship((int)(i + 1), (int)(i % 4), (int)((i * 37) % 100), (int)(i % 3)));
    double fill = now_ms() - t0;
    expect(fleet.Count() == (int)N, "fill count");

    /* ── HUD counters: 4 CountWhere scans per frame (spaceway3k pattern) ── */
    long hot = 0, alive = 0, trav = 0, merch = 0;
    t0 = now_ms();
    for (long f = 0; f < R; f++) {
        hot   += fleet.CountWhere(is_hot);
        alive += fleet.CountWhere(is_alive);
        trav  += fleet.CountWhere(is_traveling);
        merch += fleet.CountWhere(is_merchant);
    }
    double scans = now_ms() - t0;
    expect(hot > 0 && alive == N * R, "CountWhere sums");

    /* ── Where per frame (top-traders pattern) ── */
    long kept = 0;
    t0 = now_ms();
    for (long f = 0; f < R; f++) {
        auto w = fleet.Where(is_hot);
        kept += w.Count();
    }
    double where = now_ms() - t0;
    expect(kept == hot, "Where total == hot CountWhere");

    /* ── int pipeline: fill + Filter + Distinct ── */
    t0 = now_ms();
    auto nums = List<int>();
    for (long i = 0; i < N * 4; i++) nums.Add((int)(i % 97));
    auto evens = nums.Filter((int x) => x % 2 == 0);
    auto uniq  = evens.Distinct();
    double ints = now_ms() - t0;
    expect(uniq.Count() == 49, "distinct evens of 0..96");

    printf("fill:      %8.2f ms  (%ld adds)\n", fill, N);
    printf("countwhere:%8.2f ms  (%ld scans x4, %ld elems)\n", scans, R, N * R * 4);
    printf("where:     %8.2f ms  (%ld frames, kept %ld)\n", where, R, kept);
    printf("int pipe:  %8.2f ms  (fill+filter+distinct)\n", ints);
    printf("TOTAL:     %8.2f ms\n", fill + scans + where + ints);
    if (failures > 0) { printf("FAILURES: %d\n", failures); return 1; }
    printf("OK\n");
    return 0;
}
