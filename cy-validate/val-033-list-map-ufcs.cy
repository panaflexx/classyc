/* val-033-list-map-ufcs.cy — finish list/map cleanup: UFCS GroupBy + Plus.
 *
 * CLASSYC-CLEANUP.md next items for collections:
 *   · UFCS so free GroupBy(list, fn) is callable as list->GroupBy(fn)
 *   · List.Plus as non-mutating concat (operator+ stand-in)
 *   · Map missing-key docs still throw; GetOr/TryGet remain safe
 *   · ListGroupBy compat alias still works
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-033-list-map-ufcs.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int parity(int x) { return x % 2; }
int sign_of(int x) {
    if (x < 0) return -1;
    if (x > 0) return 1;
    return 0;
}
int map_parity(String k, int v) { (void)k; return v % 2; }

int main() {
    printf("=== val-033 List/Map UFCS + Plus cleanup ===\n\n");

    /* ── 1. List.Plus (non-mutating concat / operator+ stand-in) ─────────── */
    printf("-- 1. List.Plus --\n");
    List<int>* a = new List<int>{1, 2};
    defer delete a;
    List<int>* b = new List<int>{3, 4, 5};
    defer delete b;

    auto ab = a->Plus(b);
    check(ab.Count() == 5, "1a  Plus length");
    check(ab.Get(0) == 1 && ab.Get(1) == 2 && ab.Get(2) == 3
          && ab.Get(3) == 4 && ab.Get(4) == 5, "1b  Plus values");
    check(a->Count() == 2 && a->Get(0) == 1 && a->Get(1) == 2,
          "1c  left operand unchanged");
    check(b->Count() == 3, "1d  right operand unchanged");

    List<int>* empty = new List<int>();
    defer delete empty;
    auto a2 = a->Plus(empty);
    check(a2.Count() == 2 && a2.Equals(a), "1e  Plus empty right");

    auto e2 = empty->Plus(b);
    check(e2.Count() == 3 && e2.Equals(b), "1f  Plus empty left");

    /* ── 2. Concat still mutates (contrast with Plus) ────────────────────── */
    printf("\n-- 2. Concat mutates --\n");
    List<int>* c = new List<int>{10};
    defer delete c;
    List<int>* d = new List<int>{20};
    defer delete d;
    c->Concat(d);
    check(c->Count() == 2 && c->Get(1) == 20, "2a  Concat mutates left");

    /* ── 3. UFCS: list->GroupBy(fn) ───────────────────────────────────────── */
    printf("\n-- 3. UFCS list->GroupBy --\n");
    List<int>* nums = new List<int>{1, 2, 3, 4, 5, 6};
    defer delete nums;

    auto g = nums->GroupBy(parity);
    check(g.Count() == 2, "3a  GroupBy UFCS two buckets");
    check(g.Get(0)->Count() == 3, "3b  even bucket");
    check(g.Get(1)->Count() == 3, "3c  odd bucket");
    check(g.Get(0)->Get(0) == 2 && g.Get(0)->Get(2) == 6, "3d  even order");
    check(g.Get(1)->Get(0) == 1 && g.Get(1)->Get(2) == 5, "3e  odd order");

    /* free form still works */
    auto g2 = GroupBy(nums, parity);
    check(g2.Count() == 2 && g2.Get(0)->Count() == 3, "3f  free GroupBy");

    /* ListGroupBy compat alias */
    auto g3 = ListGroupBy(nums, parity);
    check(g3.Count() == 2, "3g  ListGroupBy alias");

    List<int>* signed_xs = new List<int>{-2, 0, 5, -1, 3};
    defer delete signed_xs;
    auto by_sign = signed_xs->GroupBy(sign_of);
    check(by_sign.Count() == 3, "3h  UFCS three sign buckets");
    check(by_sign.Get(-1)->Count() == 2, "3i  negatives");
    check(by_sign.Get(0)->Count() == 1,  "3j  zero");
    check(by_sign.Get(1)->Count() == 2,  "3k  positives");

    /* ── 4. Map.GroupBy still wins over free GroupBy (method precedence) ─── */
    printf("\n-- 4. Map.GroupBy instance method --\n");
    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;
    ages->Set("ada", 36);
    ages->Set("bob", 41);
    ages->Set("cy", 20);
    auto mp = ages->GroupBy(map_parity);
    check(mp.Count() == 2, "4a  Map.GroupBy method (not free UFCS)");
    check(mp.Get(0)->Count() == 2, "4b  even ages");
    check(mp.Get(1)->Count() == 1, "4c  odd ages");

    /* ── 5. Map missing-key still throws (doc cleanup regression) ────────── */
    printf("\n-- 5. Map Get throws --\n");
    int threw = 0;
    try {
        int z = ages->Get("missing");
        (void)z;
    } catch (e) {
        threw = 1;
    }
    check(threw == 1, "5a  Get(absent) throws");
    check(ages->GetOr("missing", -1) == -1, "5b  GetOr fallback");
    {
        int v = 0;
        check(!ages->TryGet("missing", &v), "5c  TryGet absent");
        check(ages->TryGet("ada", &v) && v == 36, "5d  TryGet present");
    }

    /* ── 6. Slice + Range still compose with Plus (all by-value) ─────────── */
    printf("\n-- 6. Slice / Range / Plus chain --\n");
    auto r = List<int>.Range(0, 5);
    auto mid = r.Slice(1, 3);
    auto tail = r.Slice(3, 2);
    auto joined = mid.Plus(&tail);  /* Plus takes List* */
    check(joined.Count() == 5
          && joined.Get(0) == 1 && joined.Get(2) == 3
          && joined.Get(3) == 3 && joined.Get(4) == 4,
          "6a  Slice+Plus composition");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
