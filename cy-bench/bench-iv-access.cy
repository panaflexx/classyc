/* bench-iv-access.cy — R1 guard-elision benchmark
 *
 * Counted-loop element access patterns that R1 makes guard-free:
 *   A. for (i = 0; i < xs.Count(); i++) s += xs.Get(i)
 *   B. int n = xs.Count(); for (i=0;i<n;i++) s += xs[i]
 *   C. for (i = 0; i < N; i++) s += a[i]              (C array)
 * Old compiler: real Get call + internal bounds throw per element.
 * New compiler: open-coded field loads, no traps (midopt IV proof).
 *
 * Usage:  classyc -I include cy-bench/bench-iv-access.cy -eg
 */

#include <stdio.h>
#include <time.h>
#include "list.h"

#ifndef BENCH_N
#define BENCH_N 10000
#endif
#ifndef BENCH_R
#define BENCH_R 3000
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

int main() {
    long N = BENCH_N, R = BENCH_R;
    printf("=== IV access bench  (N=%ld R=%ld) ===\n", N, R);

    auto xs = List<int>();
    for (long i = 0; i < N; i++) xs.Add((int)(i % 97));
    int a[512];
    for (int i = 0; i < 512; i++) a[i] = i;

    long total = 0;
    double t0 = now_ms();
    for (long r = 0; r < R; r++) {
        long s = 0;
        for (int i = 0; i < xs.Count(); i++) s += xs.Get(i);
        total += s & 0xFF;
    }
    double get_ms = now_ms() - t0;

    t0 = now_ms();
    int n = xs.Count();
    for (long r = 0; r < R; r++) {
        long s = 0;
        for (int i = 0; i < n; i++) s += xs[i];
        total += s & 0xFF;
    }
    double sub_ms = now_ms() - t0;

    t0 = now_ms();
    for (long r = 0; r < R; r++) {
        long s = 0;
        for (int i = 0; i < 512; i++) s += a[i];
        total += s & 0xFF;
    }
    double arr_ms = now_ms() - t0;

    printf("Get(i):   %8.2f ms\n", get_ms);
    printf("xs[i]:    %8.2f ms\n", sub_ms);
    printf("a[i]:     %8.2f ms\n", arr_ms);
    printf("checksum: %ld\n", total);
    return 0;
}
