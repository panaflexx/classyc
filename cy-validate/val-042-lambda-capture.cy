/* val-042-lambda-capture.cy — capturing lambdas via HOF open-code (Strategy A).
 *
 * Free locals stay in the caller's frame when a lambda is a direct arg to
 * Where/Filter/Map/ForEach/Any/All.  Non-capturing lambdas keep thin fn ptrs.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-042-lambda-capture.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "map.h"
#include "set.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
    fflush(stdout);
}

int is_even(int x) { return (x & 1) == 0; }

/* Top-level (not nested in main) so method-body FDA does not truncate main's. */
class Box {
    int thr;
    Box(int t) { this.thr = t; }
    int count_above(List<int>* xs) {
        int t = this.thr;
        auto big = xs.Where((int x) => x > t);
        return big.Count();
    }
};

int main() {
    printf("=== val-042 lambda capture (Strategy A) ===\n\n");
    fflush(stdout);

    /* ── 1. Where with outer int ─────────────────────────────────────────── */
    printf("-- 1. List.Where capture int --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4); xs.Add(5);
        int thr = 3;
        auto big = xs.Where((int x) => x > thr);
        check(big.Count() == 2 && big.Get(0) == 4 && big.Get(1) == 5,
              "1a Where captures thr");
        thr = 1;
        auto big2 = xs.Where((int x) => x > thr);
        check(big2.Count() == 4, "1b thr re-read after mutation");
    }

    /* ── 2. Non-capturing still works ────────────────────────────────────── */
    printf("\n-- 2. non-capturing regression --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4);
        auto ev = xs.Where((int x) => (x & 1) == 0);
        check(ev.Count() == 2 && ev.Get(0) == 2, "2a non-capturing lambda Where");
        auto ev2 = xs.Where(is_even);
        check(ev2.Count() == 2, "2b function pointer Where");
    }

    /* ── 3. ForEach mutates outer sum ────────────────────────────────────── */
    printf("\n-- 3. ForEach mutates outer --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(10); xs.Add(20); xs.Add(30);
        int sum = 0;
        int bonus = 1;
        xs.ForEach((int x) => { sum += x + bonus; });
        check(sum == 63, "3a ForEach mutates sum with bonus capture");
    }

    /* ── 4. String capture + starts_with ─────────────────────────────────── */
    printf("\n-- 4. String capture --\n");
    fflush(stdout);
    {
        auto logs = List<String>();
        logs.Add("info ok");
        logs.Add("error boom");
        logs.Add("error late");
        logs.Add("debug x");
        String prefix = "error";
        auto bad = logs.Where((String s) => s.starts_with(prefix));
        check(bad.Count() == 2, "4a Where captures String prefix");
        check(strcmp(bad.Get(0), "error boom") == 0, "4b first match");
    }

    /* ── 5. Any / All ────────────────────────────────────────────────────── */
    printf("\n-- 5. Any / All --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(2); xs.Add(4); xs.Add(6);
        int floor = 5;
        check(xs.Any((int x) => x >= floor) == 1, "5a Any capture");
        check(xs.All((int x) => x >= floor) == 0, "5b All false");
        floor = 2;
        check(xs.All((int x) => x >= floor) == 1, "5c All true");
    }

    /* ── 6. Map.Where capture ────────────────────────────────────────────── */
    printf("\n-- 6. Map.Where --\n");
    fflush(stdout);
    {
        auto m = Map<String, int>();
        m["a"] = 1; m["b"] = 50; m["c"] = 99;
        int floor = 40;
        auto hot = m.Where((String k, int v) => v >= floor);
        check(hot.Count() == 2, "6a Map.Where captures floor");
    }

    /* ── 7. Filter alias + Map transform capture ─────────────────────────── */
    printf("\n-- 7. Filter / Map --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3);
        int k = 10;
        auto y = xs.Map((int x) => x * k);
        check(y.Count() == 3 && y.Get(0) == 10 && y.Get(2) == 30, "7a Map capture k");
        int thr = 15;
        auto f = y.Filter((int x) => x > thr);
        check(f.Count() == 2 && f.Get(0) == 20, "7b Filter capture thr");
    }

    /* ── 8. Chain Where → Take ───────────────────────────────────────────── */
    printf("\n-- 8. chain --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        for (int i = 1; i <= 10; i++) xs.Add(i);
        int thr = 5;
        auto q = xs.Where((int x) => x > thr).Take(3);
        check(q.Count() == 3 && q.Get(0) == 6 && q.Get(2) == 8,
              "8a Where(capture).Take chain");
    }

    /* ── 9. Capture local taken from this in a method ─────────────────────── */
    printf("\n-- 9. method body capture local --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(5); xs.Add(9);
        Box b = Box(4);
        int n = b.count_above(&xs);
        check(n == 2, "9a method captures local t from this.thr");
    }

    /* ── 10. Capturing Find ──────────────────────────────────────────────── */
    printf("\n-- 10. Find capture --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4);
        int want = 3;
        int hit = xs.Find((int x) => x == want);
        check(hit == 3, "10a Find captures want");
        int miss = xs.Find((int x) => x == 99);
        check(miss == 0, "10b Find miss is zero");
    }

    /* ── 11. Capturing Sort (comparator) ─────────────────────────────────── */
    printf("\n-- 11. Sort capture --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(3); xs.Add(1); xs.Add(4); xs.Add(1); xs.Add(5);
        int flip = 1; /* asc: flip*(a-b) */
        xs.Sort((int a, int b) => flip * (a - b));
        check(xs.Count() == 5 && xs.Get(0) == 1 && xs.Get(4) == 5,
              "11a Sort ascending with flip=1");
        flip = -1;
        xs.Sort((int a, int b) => flip * (a - b));
        check(xs.Get(0) == 5 && xs.Get(4) == 1, "11b Sort descending re-reads flip");
        /* Source list must remain valid after capturing HOF (stmtexpr slot). */
        check(xs.Count() == 5, "11c list still intact after Sort");
    }

    /* ── 12. Capturing Select<U> ──────────────────────────────────────────── */
    printf("\n-- 12. Select capture --\n");
    fflush(stdout);
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4);
        int mul = 10;
        auto ys = xs.Select<int>((int x) => x * mul);
        check(ys.Count() == 4 && ys.Get(0) == 10 && ys.Get(3) == 40,
              "12a Select captures mul");
        mul = 100;
        auto zs = xs.Select<int>((int x) => x + mul);
        check(zs.Get(0) == 101 && zs.Get(2) == 103, "12b Select re-reads mul");
        /* Receiver not stolen by stmtexpr result slot. */
        check(xs.Count() == 4 && xs.Get(0) == 1, "12c source intact after Select");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    fflush(stdout);
    return failed;
}
