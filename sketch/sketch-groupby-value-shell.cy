/* sketch-groupby-value-shell.cy — aspirational API after P6 Phase A
 *
 * NOT expected to compile until map.h GroupBy returns a value Map shell.
 * Target house style (matches Where/Take — no owned on local pipelines):
 *
 *   auto by = roster.GroupBy((Ship s) => s.SectorKey());
 *   for (auto k, bucket in by)
 *       printf("%d: %d ships\n", k, bucket->Count());
 *   // ~by deletes Map buffer + each ownsValues List* bucket
 *
 * Phase B (later): Map<G, List<V>> with value buckets — blocked on move-only
 * nested collections inside Map.
 *
 * Run (today will fail type/owned expectations):
 *   ./bin/classyc -g -I include sketch/sketch-groupby-value-shell.cy -eg
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

int main() {
    printf("=== sketch: GroupBy value Map shell ===\n\n");

    auto nums = List<int>();
    nums.Add(1); nums.Add(2); nums.Add(3); nums.Add(4);

    /* Phase A: value Map, pointer buckets */
    auto by = nums.GroupBy(parity);
    check(by.Count() == 2, "two parity buckets");
    check(by.Get(0)->Count() + by.Get(1)->Count() == 4, "all elements present");

    int seen = 0;
    for (auto k, bucket in by) {
        (void)k;
        seen += bucket->Count();
    }
    check(seen == 4, "for-in over value GroupBy map");

    /* No owned / delete — ~by at scope exit */

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
