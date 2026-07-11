/* val-039-brace-init-move.cy — ClassName(args) value construction + move-only
 * stack List assign.
 *
 *  - new List<Pt>{ Pt(1,2), Pt(3,4) }  brace-init with class ctor exprs
 *  - xs.Add(Pt(5,6))                  free-expr ctor as Add arg
 *  - ban shallow a = b for List (dtor class)
 *  - b = move a                        ownership transfer
 *  - auto c = move b
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-039-brace-init-move.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int dtors = 0;
class Pt {
    int x; int y;
    Pt(int x, int y) { this.x = x; this.y = y; }
    ~Pt() { dtors++; }
    int getX() { return x; }
    int getY() { return y; }
};

void take(Pt p) { check(p.getX() == 9, "1a take(Pt(9,1)) arg"); }

int main() {
    printf("=== val-039 brace-init + move ===\n\n");

    /* ── 1. Free ClassName(args) value ─────────────────────────────────── */
    printf("-- 1. ClassName(args) as value --\n");
    take(Pt(9, 1));
    {
        Pt p = Pt(1, 2);
        check(p.getX() == 1 && p.getY() == 2, "1b typed Pt p = Pt(1,2)");
    }

    /* ── 2. Brace-init with ctor exprs ─────────────────────────────────── */
    printf("\n-- 2. new List<Pt>{ Pt(...), ... } --\n");
    {
        List<Pt>* xs = new List<Pt>{ Pt(1, 10), Pt(2, 20), Pt(3, 30) };
        check(xs.Count() == 3, "2a brace-init count");
        check(xs.Get(0).getX() == 1 && xs.Get(2).getY() == 30, "2b brace-init elements");
        delete xs;
    }

    /* ── 3. Add(ClassName(...)) ────────────────────────────────────────── */
    printf("\n-- 3. Add(Pt(...)) --\n");
    {
        auto xs = List<Pt>();
        xs.Add(Pt(7, 8));
        check(xs.Count() == 1 && xs.Get(0).getX() == 7, "3a Add(Pt(7,8))");
    }

    /* ── 4. Shallow assign rejected ────────────────────────────────────── */
    printf("\n-- 4. shallow assign ban --\n");
    /* Compile-time: cannot assign List with dtor without move.
     * Runtime-verified path is move (section 5).  Static check lives in
     * the compiler (see create_decl / N_ASSIGN). */

    /* ── 5. move assign ────────────────────────────────────────────────── */
    printf("\n-- 5. move assign / init --\n");
    {
        auto a = List<int>();
        a.Add(1); a.Add(2); a.Add(3);
        auto b = List<int>();
        b.Add(99);
        b = move a;
        check(b.Count() == 3 && b.Get(0) == 1 && b.Get(2) == 3, "5a b = move a keeps values");
        check(a.Count() == 0, "5b moved-from a is empty");
        /* auto c = move b */
        auto c = move b;
        check(c.Count() == 3 && c.Get(1) == 2, "5c auto c = move b");
        check(b.Count() == 0, "5d moved-from b is empty");
    }
    printf("  (section 5 scope exit — no double free)\n");
    check(1, "5e no crash after move chain");

    /* ── 6. stack List brace via stack temps still works ───────────────── */
    printf("\n-- 6. named temps brace-init --\n");
    {
        Pt a = Pt(4, 5);
        Pt b = Pt(6, 7);
        List<Pt>* xs = new List<Pt>{ a, b };
        check(xs.Count() == 2 && xs.Get(1).getX() == 6, "6a named temps brace-init");
        delete xs;
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
