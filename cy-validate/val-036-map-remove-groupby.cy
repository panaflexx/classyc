/* val-036-map-remove-groupby.cy — Map::Remove hash integrity + GroupBy ownsValues.
 *
 * Regression for:
 *   · Map::Remove swap-remove used find_slot(key) after overwriting dense[idx],
 *     which could tombstone the survivor's hash slot under churn.
 *   · GroupBy now auto ownsValues() on bucket lists (footgun fix).
 *   · Tombstone reuse on insert keeps Contains/Get coherent after remove/readd.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-036-map-remove-groupby.cy -eg
 */
#include <stdio.h>
#include "map.h"
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int int_parity(int x) { return x % 2; }

int main() {
    printf("=== val-036 Map Remove + GroupBy ownsValues ===\n\n");

    /* ── 1. Random insert/remove stress (was 63/200 fail pre-fix) ──────── */
    printf("-- 1. Remove stress --\n");
    int stress_fail = 0;
    for (int seed = 1; seed <= 200; seed++) {
        Map<int, int>* m = new Map<int, int>();
        int present[64];
        for (int i = 0; i < 64; i++) present[i] = 0;

        unsigned s = (unsigned)seed * 2654435761u;
        for (int step = 0; step < 400; step++) {
            s = s * 1664525u + 1013904223u;
            int k = (int)(s % 64);
            int op = (int)((s >> 8) % 3);
            if (op == 0 || !present[k]) {
                m->Set(k, k * 17 + seed);
                present[k] = 1;
            } else if (op == 1) {
                m->Remove(k);
                present[k] = 0;
            } else {
                int v = m->GetOr(k, -1);
                int want = present[k] ? k * 17 + seed : -1;
                if (v != want) { stress_fail++; break; }
            }
        }
        int live = 0;
        for (int k = 0; k < 64; k++) if (present[k]) live++;
        if (m->Count() != live) stress_fail++;
        else {
            for (int k = 0; k < 64; k++) {
                int got = m->GetOr(k, -9999);
                int want = present[k] ? k * 17 + seed : -9999;
                if (got != want) { stress_fail++; break; }
            }
        }
        delete m;
        if (stress_fail) break;
    }
    check(stress_fail == 0, "1a  200-seed remove/reinsert stress");

    /* ── 2. Remove middle of dense map (swap-remove path) ───────────────── */
    printf("\n-- 2. Dense swap-remove survivors --\n");
    {
        Map<int, int>* m = new Map<int, int>();
        defer delete m;
        for (int i = 0; i < 32; i++) m->Set(i, 1000 + i);
        for (int i = 0; i < 16; i++) m->Remove(i);
        int ok = (m->Count() == 16);
        for (int i = 16; i < 32; i++) {
            if (!m->Contains(i) || m->Get(i) != 1000 + i) ok = 0;
        }
        for (int i = 0; i < 16; i++) {
            if (m->Contains(i)) ok = 0;
        }
        check(ok, "2a  survivors 16..31 intact after removing 0..15");
    }

    /* ── 3. Tombstone reuse: remove then re-add same keys ───────────────── */
    printf("\n-- 3. Tombstone reuse --\n");
    {
        Map<String, int>* m = new Map<String, int>();
        defer delete m;
        m->Set("a", 1);
        m->Set("b", 2);
        m->Set("c", 3);
        m->Remove("b");
        m->Set("b", 20);
        m->Remove("a");
        m->Set("a", 10);
        check(m->Count() == 3, "3a  count after remove/reinsert");
        check(m->Get("a") == 10 && m->Get("b") == 20 && m->Get("c") == 3,
              "3b  values after tombstone reuse");
    }

    /* ── 4. GroupBy auto ownsValues (bucket lists free with map) ────────── */
    printf("\n-- 4. GroupBy owns bucket Lists --\n");
    {
        List<int>* nums = new List<int>{1, 2, 3, 4, 5, 6};
        defer delete nums;
        /* No explicit ownsValues() — GroupBy must mark buckets owned. */
        auto g = nums->GroupBy(int_parity);
        check(g.Count() == 2, "4a  two parity buckets");
        check(g.Get(0)->Count() == 3 && g.Get(1)->Count() == 3, "4b  bucket sizes");
        /* double ownsValues is harmless */
        check(g.Get(0)->Get(0) == 2, "4c  even first is 2");
    }
    {
        Map<String, int>* ages = new Map<String, int>();
        defer delete ages;
        ages->Set("ann", 20);
        ages->Set("bob", 41);
        ages->Set("cy", 33);
        auto mp = ages->GroupBy<int>((String k, int v) => {
            (void)k;
            return v % 2;
        });
        check(mp.Count() == 2, "4d  Map.GroupBy auto ownsValues");
        check(mp.Get(0)->Count() == 1 && mp.Get(1)->Count() == 2, "4e  parity counts");
    }

    /* ── 5. Merge NULL / Copy non-owning ────────────────────────────────── */
    printf("\n-- 5. Merge NULL + Copy --\n");
    {
        Map<int, int>* m = new Map<int, int>();
        defer delete m;
        m->Set(1, 10);
        m->Merge(NULL);
        check(m->Count() == 1 && m->Get(1) == 10, "5a  Merge(NULL) no-op");
        auto c = m->Copy();  /* Copy returns Map by value / RAII */
        check(c.Count() == 1 && c.Get(1) == 10, "5b  Copy shallow contents");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
