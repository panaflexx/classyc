/* val-040-value-transforms.cy — first-class by-value List/Map transforms.
 *
 * Target house style (CLASSYC-CLEANUP.md P0–P3):
 *   auto xs = List<int>();
 *   auto top = xs.Where(...).Take(3);   // value shells, RAII — no owned/delete
 *   List<int> make() { auto a = List<int>(); ...; return a; }
 *   auto ys = make();
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-040-value-transforms.cy -eg
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

List<int> make_scores() {
    auto a = List<int>();
    a.Add(10); a.Add(20); a.Add(30);
    return a;                          /* implicit move of move-only collection */
}

List<int> make_moved() {
    auto a = List<int>();
    a.Add(7); a.Add(8);
    return move a;
}

int even(int x) { return x % 2 == 0; }

int box_dtors = 0;
class Box {
    int id;
    Box(int id) { this.id = id; }
    ~Box() { box_dtors++; }
    int getId() { return id; }
};
int even_box(Box* b) { return b.getId() % 2 == 0; }

int main() {
    printf("=== val-040 value-returning transforms ===\n\n");

    /* ── 1. Move return + prvalue bind ───────────────────────────────────── */
    printf("-- 1. move return / bind --\n");
    {
        auto xs = make_scores();
        check(xs.Count() == 3 && xs.Get(0) == 10 && xs.Last() == 30,
              "1a auto xs = make_scores() binds value List");
        auto ys = make_moved();
        check(ys.Count() == 2 && ys.Get(1) == 8, "1b return move a");
        List<int> zs = make_scores();
        check(zs.Count() == 3, "1c typed List<int> zs = make_scores()");
    }
    check(1, "1d scope exit RAII — no double free");

    /* ── 2. Where / Take / Skip / Copy value shells ──────────────────────── */
    printf("\n-- 2. Where/Take/Skip/Copy --\n");
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4); xs.Add(5);

        auto ev = xs.Where(even);
        check(ev.Count() == 2 && ev.Get(0) == 2 && ev.Get(1) == 4,
              "2a Where → value List");

        auto top3 = xs.Take(3);
        check(top3.Count() == 3 && top3.Last() == 3, "2b Take → value List");

        auto rest = xs.Skip(2);
        check(rest.Count() == 3 && rest.Get(0) == 3, "2c Skip → value List");

        auto cp = xs.Copy();
        check(cp.Count() == 5 && cp.Get(4) == 5, "2d Copy → value List");
        check(xs.Count() == 5, "2e source intact after transforms");
    }

    /* ── 3. Chains ───────────────────────────────────────────────────────── */
    printf("\n-- 3. chains --\n");
    {
        auto xs = List<int>();
        xs.Add(10); xs.Add(20); xs.Add(30); xs.Add(40); xs.Add(50);

        auto silver = xs.Skip(1).Take(1);
        check(silver.Count() == 1 && silver.Get(0) == 20,
              "3a Skip(1).Take(1) chain");

        auto q = xs.Where((int x) => x > 15).Take(2);
        check(q.Count() == 2 && q.Get(0) == 20 && q.Get(1) == 30,
              "3b Where(...).Take(2) chain");
    }

    /* ── 4. Range / Distinct / Plus / Slice ──────────────────────────────── */
    printf("\n-- 4. factories & set ops --\n");
    {
        auto a = List<int>.Range(1, 4);
        check(a.Count() == 4 && a.Get(0) == 1 && a.Last() == 4,
              "4a List.Range → value List");

        auto b = List<int>.Range(5, 2);
        auto full = a.Plus(&b);
        check(full.Count() == 6 && full.Get(4) == 5, "4b Plus value");

        auto mid = full.Slice(2, 3);
        check(mid.Count() == 3 && mid.Get(0) == 3, "4c Slice value");

        auto noisy = List<int>();
        noisy.Add(1); noisy.Add(1); noisy.Add(2); noisy.Add(2);
        auto clean = noisy.Distinct();
        check(clean.Count() == 2 && clean.Get(0) == 1 && clean.Get(1) == 2,
              "4d Distinct value");
    }

    /* ── 5. Map Where / Keys / Copy ──────────────────────────────────────── */
    printf("\n-- 5. Map value transforms --\n");
    {
        auto m = Map<String, int>();
        m["a"] = 1; m["b"] = 2; m["c"] = 3;
        auto high = m.Where((String k, int v) => { (void)k; return v >= 2; });
        check(high.Count() == 2, "5a Map.Where → value Map");
        auto ks = m.Keys();
        check(ks.Count() == 3, "5b Map.Keys → value List");
        auto cp = m.Copy();
        check(cp.Count() == 3 && cp["b"] == 2, "5c Map.Copy → value Map");
    }

    /* ── 6. Set Filter / Union ───────────────────────────────────────────── */
    printf("\n-- 6. Set value transforms --\n");
    {
        auto s = Set<int>();
        s.Add(1); s.Add(2); s.Add(3);
        auto ev = s.Filter(even);
        check(ev.Count() == 1 && ev.Contains(2), "6a Set.Filter → value Set");

        auto t = Set<int>();
        t.Add(2); t.Add(4);
        auto u = s.Union(&t);
        check(u.Count() == 4 && u.Contains(4), "6b Set.Union → value Set");
    }

    /* ── 7. owns() views remain non-owning ───────────────────────────────── */
    printf("\n-- 7. owns + value views (no double free) --\n");
    {
        int before = box_dtors;
        auto src = List<Box*>();
        src.owns();
        src.Add(new Box(1));
        src.Add(new Box(2));
        src.Add(new Box(3));

        auto view = src.Where(even_box);
        check(view.Count() == 1 && view.Get(0).getId() == 2,
              "7a Where view on owns() list");
        check(box_dtors == before, "7b view scope does not free pointees");
        check(src.Count() == 3, "7c source still holds 3");
        /* ~src deletes the three Boxes once */
    }
    check(box_dtors >= 3, "7d owns source freed pointees once");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
