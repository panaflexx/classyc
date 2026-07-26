/* val-056-byval-gate-lifetime.cy — by-value element gate + prvalue temp
 * lifetimes (BY-VALUE.md P0/P1/P5, compiler-validated).
 *
 *  - [[copyable_no_release]] lets a quiet-dtor class live in List/Map/Set
 *  - take(Box(7)) prvalue call arg: temp destroyed exactly once
 *  - Box(9).getId() prvalue receiver: temp destroyed exactly once
 *  - xs.Add(Box(..)) / brace-init: adopted, container destroys exactly once
 *  - C array of dtor class: elements destroyed at scope exit (P5)
 *
 * The negative gate case (resource-owning dtor class in List<T> must be a
 * compile error) is covered by sketch/probe-p1-relocate-gate.cy, which now
 * fails to compile with a "not marked [[copyable_no_release]]" diagnostic.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-056-byval-gate-lifetime.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"
#include "set.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int ctors = 0, dtors = 0;

[[copyable_no_release]] /* counting dtor only — no owned resource */
class Box {
    int id;
    Box(int id) { this.id = id; ctors++; }
    ~Box() { dtors++; }
    int getId() { return id; }
};

void take(Box b) { check(b.getId() == 7, "1a take(Box(7)) param value"); }

int main() {
    printf("=== val-056 by-value gate + prvalue lifetime ===\n\n");

    /* ── 1. prvalue call arg destroyed exactly once ────────────────────── */
    printf("-- 1. take(Box(7)) --\n");
    {
        int c0 = ctors, d0 = dtors;
        take(Box(7));
        check(ctors - c0 == 1 && dtors - d0 == 1, "1b prvalue arg destroyed after call");
    }

    /* ── 2. prvalue receiver destroyed exactly once ────────────────────── */
    printf("\n-- 2. Box(9).getId() --\n");
    {
        int c0 = ctors, d0 = dtors;
        int v = Box(9).getId();
        check(v == 9, "2a value correct");
        check(ctors - c0 == 1 && dtors - d0 == 1, "2b receiver temp destroyed");
    }

    /* ── 3. Add adopts: container destroys each element exactly once ───── */
    printf("\n-- 3. xs.Add(Box(..)) x2 --\n");
    {
        int c0 = ctors, d0 = dtors;
        {
            auto xs = List<Box>();
            xs.Add(Box(10));
            xs.Add(Box(20));
            check(xs.Count() == 2 && xs.Get(1).getId() == 20, "3a elements intact");
        }
        check(ctors - c0 == 2 && dtors - d0 == 2, "3b adopted temps destroyed once");
    }

    /* ── 4. attribute opt-in across Map and Set ────────────────────────── */
    printf("\n-- 4. Map<String, Box> / Set<Box> compile + work --\n");
    {
        auto m = Map<String, Box>();
        m.Set("a", Box(1));
        check(m.Get("a").getId() == 1, "4a Map value");
        auto s = Set<Box>();
        s.Add(Box(2));
        check(s.Count() == 1, "4b Set element");
    }

    /* ── 5. C array elements destroyed at scope exit (P5) ──────────────── */
    printf("\n-- 5. Box arr[2] scope exit --\n");
    {
        int d0 = dtors;
        {
            Box arr[2];
            arr[0] = Box(1);
            arr[1] = Box(2);
            check(arr[0].getId() == 1 && arr[1].getId() == 2, "5a values intact");
        }
        check(dtors - d0 == 2, "5b array elements destroyed at scope exit");
    }

    /* ── 6. global balance ─────────────────────────────────────────────── */
    printf("\n-- 6. global --\n");
    printf("  total ctors=%d dtors=%d\n", ctors, dtors);
    check(ctors == dtors, "6a ctor == dtor overall");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
