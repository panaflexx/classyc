/* probe-p0-prvalue-lifetime.cy — STRICT ctor/dtor accounting for class
 * prvalue temporaries (BY-VALUE.md P0).
 *
 * Copies are bitwise (no copy ctor), so ctors counts ONLY explicit
 * construction.  If lifetime handling is correct:
 *      dtors == ctors        at every checkpoint
 * dtors > ctors  → double-destroy (temp dtor + element/param dtor)
 * dtors < ctors  → leaked temporary (never destroyed)
 *
 * Run: ./bin/classyc -g -I include sketch/probe-p0-prvalue-lifetime.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int ctors = 0, dtors = 0;
[[copyable_no_release]] /* counting dtor only */
class Box {
    int id;
    Box(int id) { this.id = id; ctors++; }
    ~Box() { dtors++; }
    int getId() { return id; }
};

void take(Box b) { check(b.getId() == 7, "2a take(Box(7)) param value"); }

int main() {
    printf("=== probe P0: strict prvalue lifetime ===\n\n");

    /* ── 1. named stack object: baseline ───────────────────────────────── */
    printf("-- 1. named stack Box --\n");
    {
        Box a = Box(1);
        check(ctors == 1 && dtors == 0, "1a constructed, alive in scope");
    }
    printf("  after scope: ctors=%d dtors=%d\n", ctors, dtors);
    check(ctors == 1 && dtors == 1, "1b dtor exactly once at scope exit");

    /* ── 2. prvalue as call arg ────────────────────────────────────────── */
    printf("\n-- 2. take(Box(7)) --\n");
    {
        int c0 = ctors, d0 = dtors;
        take(Box(7));
        printf("  delta: +ctors=%d +dtors=%d\n", ctors - c0, dtors - d0);
        check(ctors == dtors, "2b all constructed Boxes destroyed after call");
    }

    /* ── 3. prvalue as List.Add arg ────────────────────────────────────── */
    printf("\n-- 3. xs.Add(Box(...)) x2 --\n");
    {
        int c0 = ctors, d0 = dtors;
        {
            auto xs = List<Box>();
            xs.Add(Box(10));
            xs.Add(Box(20));
            check(xs.Count() == 2, "3a list has 2 elements");
            check(xs.Get(0).getId() == 10 && xs.Get(1).getId() == 20,
                  "3b element values intact");
        }
        printf("  delta: +ctors=%d +dtors=%d\n", ctors - c0, dtors - d0);
        check(ctors == dtors, "3c exact dtor accounting after Add + scope exit");
    }

    /* ── 4. brace-init with prvalues ───────────────────────────────────── */
    printf("\n-- 4. new List<Box>{ Box(1), Box(2) } --\n");
    {
        int c0 = ctors, d0 = dtors;
        {
            List<Box>* xs = new List<Box>{ Box(1), Box(2) };
            check(xs.Count() == 2, "4a brace count");
            delete xs;
        }
        printf("  delta: +ctors=%d +dtors=%d\n", ctors - c0, dtors - d0);
        check(ctors == dtors, "4b exact dtor accounting after brace-init");
    }

    /* ── 5. global balance ─────────────────────────────────────────────── */
    printf("\n-- 5. global --\n");
    printf("  total ctors=%d dtors=%d\n", ctors, dtors);
    check(ctors == dtors, "5a ctor == dtor overall");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
