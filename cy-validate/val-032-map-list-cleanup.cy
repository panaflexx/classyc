/* val-032-map-list-cleanup.cy — Map higher-order + List Range cleanup.
 *
 * Covers the remaining CLASSYC-CLEANUP.md Map ergonomics and List follow-ups:
 *   Map: GetOrAdd, ContainsValue, AddOrUpdate, Where*, Any/All,
 *        SelectValues<W>, SelectKeys<G>, GroupBy<G>, int-key ToJson
 *   List: Range factory, Slice, Concat chain
 *   Free: ListGroupBy / GroupBy (map.h; method form via UFCS in val-033)
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-032-map-list-cleanup.cy -eg
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

/* ── predicates / projectors ─────────────────────────────────────────── */

int map_val_gt_10(String k, int v) { (void)k; return v > 10; }
int map_key_starts_a(String k) {
    const char *s = (const char *)k;
    return s && s[0] == 'a';
}
int map_val_even(int v) { return v % 2 == 0; }
int map_any_pos(String k, int v) { (void)k; return v > 0; }
int map_all_pos(String k, int v) { (void)k; return v > 0; }
int map_double_val(String k, int v) { (void)k; return v * 2; }
/* Stable String keys for SelectKeys (avoid arena temporaries as map keys). */
int map_key_code(String k, int v) {
    (void)v;
    const char *s = (const char *)k;
    return (s && s[0]) ? (int)s[0] : 0;
}
int map_parity_group(String k, int v) { (void)k; return v % 2; }

int list_parity(int x) { return x % 2; }
int list_sign(int x) {
    if (x < 0) return -1;
    if (x > 0) return 1;
    return 0;
}

int bump_age(int v) { return v + 1; }

int main() {
    printf("=== val-032 Map + List cleanup ===\n\n");

    /* ── 1. Map GetOrAdd / ContainsValue / AddOrUpdate ─────────────────── */
    printf("-- 1. Map GetOrAdd / ContainsValue / AddOrUpdate --\n");
    Map<String, int>* m = new Map<String, int>();
    defer delete m;
    m->Set("ada", 36);
    m->Set("bob", 40);

    check(m->GetOrAdd("ada", 0) == 36, "1a  GetOrAdd existing returns value");
    check(m->Count() == 2,             "1b  GetOrAdd existing does not grow");
    check(m->GetOrAdd("zoe", 9) == 9,  "1c  GetOrAdd absent inserts fallback");
    check(m->Count() == 3,             "1d  GetOrAdd absent grew");
    check(m->ContainsValue(40) == true,  "1e  ContainsValue present");
    check(m->ContainsValue(99) == false, "1f  ContainsValue absent");

    int updated = m->AddOrUpdate("ada", 1, bump_age);
    check(updated == 0 && m->Get("ada") == 37, "1g  AddOrUpdate existing runs updater");
    int inserted = m->AddOrUpdate("cy", 7, bump_age);
    check(inserted == 1 && m->Get("cy") == 7,  "1h  AddOrUpdate new inserts val");

    /* ── 2. Map Where / Any / All ──────────────────────────────────────── */
    printf("\n-- 2. Map Where / Any / All --\n");
    Map<String, int>* base = new Map<String, int>();
    defer delete base;
    base->Set("apple", 12);
    base->Set("bee", 3);
    base->Set("ant", 8);
    base->Set("zoo", 20);

    Map<String, int>* w = base->Where(map_val_gt_10);
    defer delete w;
    check(w->Count() == 2, "2a  Where(val>10) count");
    check(w->Contains("apple") && w->Contains("zoo"), "2b  Where keeps matching keys");

    Map<String, int>* wk = base->WhereKeys(map_key_starts_a);
    defer delete wk;
    check(wk->Count() == 2 && wk->Contains("apple") && wk->Contains("ant"),
          "2c  WhereKeys 'a*'");

    Map<String, int>* wv = base->WhereValues(map_val_even);
    defer delete wv;
    check(wv->Count() == 3 && wv->Contains("apple") && wv->Contains("ant") && wv->Contains("zoo"),
          "2d  WhereValues even");

    check(base->Any(map_any_pos) == 1, "2e  Any positive");
    check(base->All(map_all_pos) == 1, "2f  All positive");
    base->Set("neg", -1);
    check(base->All(map_all_pos) == 0, "2g  All fails with negative");
    base->Remove("neg");

    /* ── 3. Map SelectValues / SelectKeys (generic methods) ────────────── */
    printf("\n-- 3. Map SelectValues / SelectKeys --\n");
    Map<String, int>* src = new Map<String, int>();
    defer delete src;
    src->Set("a", 1);
    src->Set("b", 2);
    src->Set("c", 3);

    Map<String, int>* doubled = src->SelectValues<int>(map_double_val);
    defer delete doubled;
    check(doubled->Count() == 3, "3a  SelectValues count");
    check(doubled->Get("a") == 2 && doubled->Get("b") == 4 && doubled->Get("c") == 6,
          "3b  SelectValues values");

    Map<int, int>* coded = src->SelectKeys<int>(map_key_code);
        defer delete coded;
        check(coded->Count() == 3, "3c  SelectKeys count");
        check(coded->Contains((int)'a') && coded->Contains((int)'b') && coded->Contains((int)'c'),
              "3d  SelectKeys char codes");
        check(coded->Get((int)'a') == 1 && coded->Get((int)'c') == 3, "3e  SelectKeys values intact");

    /* ── 4. Map GroupBy ───────────────────────────────────────────────── */
    printf("\n-- 4. Map GroupBy --\n");
    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;
    ages->Set("ada", 36);
    ages->Set("bob", 41);
    ages->Set("cy", 20);
    ages->Set("dee", 33);

    Map<int, List<int>*>* parity = ages->GroupBy<int>(map_parity_group);
        parity->ownsValues();
        defer delete parity;
    check(parity->Count() == 2, "4a  GroupBy two parity buckets");
    List<int>* evens = parity->GetOr(0, NULL);
    List<int>* odds  = parity->GetOr(1, NULL);
    check(evens != NULL && odds != NULL, "4b  both buckets present");
    check(evens->Count() == 2, "4c  even ages count");
    check(odds->Count() == 2,  "4d  odd ages count");

    /* ── 5. Map int-key ToJson / to_string ─────────────────────────────── */
    printf("\n-- 5. Map int-key / String-key JSON --\n");
    Map<int, String>* names = new Map<int, String>();
    defer delete names;
    names->Set(1, "one");
    names->Set(2, "two");
    String ij = names->ToJson();
    /* keys become decimal strings */
    check(ij != NULL && ((String)ij).contains("\"1\""), "5a  int-key ToJson has \"1\"");
    check(((String)ij).contains("one") && ((String)ij).contains("two"),
          "5b  int-key ToJson values");

    Map<String, int>* sc = new Map<String, int>();
    defer delete sc;
    sc->Set("x", 1);
    sc->Set("y", 2);
    String sj = sc->to_string();
    check(sj != NULL && ((String)sj).contains("\"x\""), "5c  to_string via ToJson");

    Map<double, int>* bad = new Map<double, int>();
    defer delete bad;
    bad->Set(1.5, 9);
    String emptyj = bad->ToJson();
    check(emptyj != NULL && strcmp((char*)emptyj, "{}") == 0,
          "5d  non-String/int keys ToJson -> {}");

    /* ── 6. List.Range ────────────────────────────────────────────────── */
    printf("\n-- 6. List.Range --\n");
    List<int>* r = List<int>.Range(3, 4);
    defer delete r;
    check(r->Count() == 4, "6a  Range count");
    check(r->Get(0) == 3 && r->Get(1) == 4 && r->Get(2) == 5 && r->Get(3) == 6,
          "6b  Range values");

    List<int>* r0 = List<int>.Range(0, 0);
    defer delete r0;
    check(r0->Count() == 0, "6c  Range(0,0) empty");

    int range_threw = 0;
        try {
            unowned List<int>* badR = List<int>.Range(0, -1);
            (void)badR;
        } catch (e) {
            range_threw = 1;
        }
        check(range_threw == 1, "6d  Range negative count throws");

    /* ── 7. ListGroupBy free fn (map.h; avoids list↔map include cycle) ── */
        printf("\n-- 7. ListGroupBy --\n");
        List<int>* nums = new List<int>{1, 2, 3, 4, 5, 6};
        defer delete nums;
        Map<int, List<int>*>* g = ListGroupBy(nums, list_parity);
            g->ownsValues();
            defer delete g;
        check(g->Count() == 2, "7a  ListGroupBy parity buckets");
        List<int>* g0 = g->Get(0);
        List<int>* g1 = g->Get(1);
        check(g0->Count() == 3 && g1->Count() == 3, "7b  three even / three odd");
        check(g0->Get(0) == 2 && g0->Get(1) == 4 && g0->Get(2) == 6, "7c  even order");
        check(g1->Get(0) == 1 && g1->Get(1) == 3 && g1->Get(2) == 5, "7d  odd order");

        List<int>* signed_xs = new List<int>{-2, 0, 5, -1, 3};
        defer delete signed_xs;
        Map<int, List<int>*>* by_sign = ListGroupBy(signed_xs, list_sign);
            by_sign->ownsValues();
            defer delete by_sign;
        check(by_sign->Count() == 3, "7e  ListGroupBy sign three buckets");
        check(by_sign->Get(-1)->Count() == 2, "7f  negatives");
        check(by_sign->Get(0)->Count() == 1,  "7g  zeros");
        check(by_sign->Get(1)->Count() == 2,  "7h  positives");

    /* ── 8. List Slice + Concat remaining cleanup ─────────────────────── */
    printf("\n-- 8. List Slice / Concat --\n");
    List<int>* full = new List<int>{10, 20, 30, 40, 50};
    defer delete full;
    List<int>* mid = full->Slice(1, 3);
    defer delete mid;
    check(mid->Count() == 3 && mid->Get(0) == 20 && mid->Get(2) == 40,
          "8a  Slice(1,3)");

    List<int>* a = new List<int>{1, 2};
    defer delete a;
    List<int>* b = new List<int>{3, 4};
    defer delete b;
    a->Concat(b);
    check(a->Count() == 4 && a->Get(2) == 3 && a->Get(3) == 4, "8b  Concat appends");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
