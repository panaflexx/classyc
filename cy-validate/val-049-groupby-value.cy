/* val-049-groupby-value.cy — GroupBy returns value Map shell (no owned/delete).
 *
 * Phase A of CLASSYC-CLEANUP P6:
 *   auto g = nums.GroupBy(parity);   // RAII Map; ownsValues List* buckets
 *   // no owned / defer delete on g
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-049-groupby-value.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int parity(int x) { return x & 1; }

int main(void) {
    printf("=== val-049 GroupBy value Map shell ===\n\n");

    /* ── 1. Stack List UFCS GroupBy ──────────────────────────────────────── */
    printf("-- 1. stack List.GroupBy --\n");
    {
        auto nums = List<int>();
        nums.Add(1); nums.Add(2); nums.Add(3); nums.Add(4); nums.Add(5); nums.Add(6);
        auto g = nums.GroupBy(parity);
        check(g.Count() == 2, "1a two parity buckets");
        check(g.Get(0)->Count() == 3 && g.Get(1)->Count() == 3, "1b bucket sizes");
        check(g.Get(0)->Get(0) == 2 && g.Get(1)->Get(0) == 1, "1c first elements");
        int seen = 0;
        for (auto k, bucket in g) {
            (void)k;
            seen += bucket->Count();
        }
        check(seen == 6, "1d for-in visits all");
        /* ~g frees map + ownsValues buckets — no owned/delete */
    }

    /* ── 2. Free GroupBy / ListGroupBy ───────────────────────────────────── */
    printf("\n-- 2. free GroupBy / ListGroupBy --\n");
    {
        auto nums = List<int>();
        nums.Add(10); nums.Add(11); nums.Add(12);
        auto g2 = GroupBy(&nums, parity);
        check(g2.Count() == 2, "2a free GroupBy");
        auto g3 = ListGroupBy(&nums, parity);
        check(g3.Count() == 2 && g3.Get(0)->Count() == 2, "2b ListGroupBy");
    }

    /* ── 3. Map.GroupBy value shell ──────────────────────────────────────── */
    printf("\n-- 3. Map.GroupBy --\n");
    {
        auto ages = Map<String, int>();
        ages["ada"] = 36;
        ages["bob"] = 41;
        ages["cy"] = 20;
        auto mp = ages.GroupBy((String k, int v) => { (void)k; return v % 2; });
        check(mp.Count() == 2, "3a Map.GroupBy two buckets");
        check(mp.Get(0)->Count() == 2 && mp.Get(1)->Count() == 1, "3b parity counts");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
