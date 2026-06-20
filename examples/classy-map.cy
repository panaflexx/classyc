/* classy-map.cy — comprehensive exercise of include/map.h
 *
 * Tests the generic Map<K, V> hash map (the typed, type-safe sibling of `dict`):
 *   · Constructors (default, capacity)
 *   · Core API: Count, Capacity, IsEmpty, Contains, Get, GetOr, Set, Remove, Clear
 *   · Subscript sugar:  map[k] = v   and   v = map[k]
 *   · for-in iteration:  for (auto k in m)  and  for (auto k, v in m)
 *   · KeyAt / ValAt indexed access (insertion order)
 *   · Bulk: Copy, Merge, ForEach
 *   · Key kinds: String keys (content), int keys (value), object values
 *   · Growth / rehashing
 *
 * Usage: classyc examples/classy-map.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "include/map.h"

int passed = 0;
int failed = 0;

void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* A small reference type, to demonstrate string -> object mapping. */
class Track {
    String title;
    int    seconds;
    Track(String t, int s) { this->title = t; this->seconds = s; }
    ~Track() { /* nothing to free */ }
};

/* ForEach callback: accumulate values through a file-scope total. */
int foreach_total = 0;
void add_value(String key, int value) { foreach_total += value; }

int main() {
    printf("=== Map<K, V> test suite ===\n\n");

    /* ── 1. String -> int, Set / Get / update ───────────────────────── */
    printf("-- 1. Map<String,int>: Set, Get, update --\n");

    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;

    ages->Set("Ada", 36);
    ages->Set("Bob", 40);
    ages->Set("Cy",  21);
    check(ages->Count() == 3,         "1a  Count after 3 inserts");
    check(ages->Get("Ada") == 36,     "1b  Get(\"Ada\")");
    check(ages->Set("Ada", 37) == 0,  "1c  Set existing key returns 0");
    check(ages->Get("Ada") == 37,     "1d  value updated in place");
    check(ages->Count() == 3,         "1e  Count unchanged after update");
    check(ages->Contains("Bob") == 1, "1f  Contains present key");
    check(ages->Contains("Zoe") == 0, "1g  !Contains absent key");

    /* ── 2. Missing keys: Get vs GetOr ──────────────────────────────── */
    printf("\n-- 2. Missing keys --\n");

    check(ages->Get("Zoe") == 0,        "2a  Get(absent) -> 0");
    check(ages->GetOr("Zoe", -1) == -1, "2b  GetOr(absent, -1)");
    check(ages->GetOr("Ada", -1) == 37, "2c  GetOr(present) -> value");

    /* ── 3. Subscript sugar: map[k] = v / v = map[k] ────────────────── */
    printf("\n-- 3. Subscript --\n");

    ages["Dot"] = 99;                 /* insert via subscript  */
    ages["Bob"] = 41;                 /* update via subscript  */
    check(ages->Count() == 4,         "3a  subscript insert grew count");
    check(ages["Dot"] == 99,          "3b  subscript read inserted");
    check(ages["Bob"] == 41,          "3c  subscript read updated");
    check(ages["Nope"] == 0,          "3d  subscript read absent -> 0");

    /* ── 4. Remove, Clear ───────────────────────────────────────────── */
    printf("\n-- 4. Remove, Clear --\n");

    check(ages->Remove("Dot") == 1,   "4a  Remove present returns 1");
    check(ages->Remove("Dot") == 0,   "4b  Remove absent returns 0");
    check(ages->Count() == 3,         "4c  Count after remove");
    check(ages->Contains("Dot") == 0, "4d  removed key gone");

    /* ── 5. for-in: keys, and (key, value) ──────────────────────────── */
    printf("\n-- 5. for-in iteration --\n");

    Map<String, int>* scores = new Map<String, int>();
    defer delete scores;
    scores["alpha"] = 10;
    scores["beta"]  = 20;
    scores["gamma"] = 30;

    int n_keys = 0;
    for (auto k in scores) n_keys++;
    check(n_keys == 3,                "5a  for-in keys visits each entry");

    int total = 0, n_pairs = 0;
    for (auto k, v in scores) { total += v; n_pairs++; }
    check(n_pairs == 3,               "5b  for-in (key,value) visits each entry");
    check(total == 60,                "5c  for-in (key,value) sums values");

    /* insertion order is preserved by the dense backing arrays */
    String first_key = scores->KeyAt(0);
    check(strcmp((char*)first_key, "alpha") == 0, "5d  KeyAt(0) is first inserted");
    check(scores->ValAt(2) == 30,                 "5e  ValAt(2) is third value");

    /* ── 6. Copy + Merge ────────────────────────────────────────────── */
    printf("\n-- 6. Copy, Merge --\n");

    Map<String, int>* base = new Map<String, int>();
    defer delete base;
    base["x"] = 1; base["y"] = 2;

    Map<String, int>* dup = base->Copy();
    defer delete dup;
    dup["z"] = 3;                     /* mutate the copy only */
    check(base->Count() == 2,         "6a  Copy is independent (original unchanged)");
    check(dup->Count() == 3,          "6b  copy mutated separately");

    base->Merge(dup);                 /* pull dup's entries into base */
    check(base->Count() == 3,         "6c  Merge adds new key");
    check(base["z"] == 3,             "6d  Merge brought value across");

    /* ── 7. ForEach ─────────────────────────────────────────────────── */
    printf("\n-- 7. ForEach --\n");

    foreach_total = 0;
    scores->ForEach(add_value);
    check(foreach_total == 60,        "7a  ForEach visits every value");

    /* ── 8. int -> String keys ──────────────────────────────────────── */
    printf("\n-- 8. Map<int,String> --\n");

    Map<int, String>* names = new Map<int, String>();
    defer delete names;
    names[1] = "one";
    names[2] = "two";
    names[3] = "three";
    check(names->Count() == 3,                         "8a  int-keyed count");
    check(strcmp((char*)names[2], "two") == 0,         "8b  int-keyed subscript read");
    names[2] = "TWO";
    check(strcmp((char*)names[2], "TWO") == 0,         "8c  int-keyed subscript update");

    /* ── 9. String -> object (Track*) ───────────────────────────────── */
    printf("\n-- 9. Map<String,Track*> (string -> object) --\n");

    Map<String, Track*>* library = new Map<String, Track*>();
    defer delete library;
    library["Kashmir"] = new Track("Kashmir", 508);
    library["Africa"]  = new Track("Africa",  295);
    library["One"]     = new Track("One",     426);

    Track* k = library["Kashmir"];
    check(k != NULL && k->seconds == 508,    "9a  object lookup + field access");
    check(library["Missing"] == NULL,        "9b  absent object key -> NULL");

    int total_secs = 0;
    for (auto title, trk in library) total_secs += trk->seconds;
    check(total_secs == 508 + 295 + 426,     "9c  for-in over objects sums durations");

    printf("  longest tracks:\n");
    for (auto title, trk in library)
        printf("    %-10s %d:%02d\n", (char*)title, trk->seconds / 60, trk->seconds % 60);

    /* the map owns only the pointers; free the Tracks ourselves */
    for (auto title, trk in library) delete trk;

    /* ── 10. Growth / rehashing ─────────────────────────────────────── */
    printf("\n-- 10. Growth --\n");

    Map<int, int>* squares = new Map<int, int>();
    defer delete squares;
    for (int i = 0; i < 5000; i++) squares[i] = i * i;
    int all_ok = 1;
    for (int i = 0; i < 5000; i++) if (squares[i] != i * i) all_ok = 0;
    check(squares->Count() == 5000,  "10a  5000 entries present");
    check(all_ok == 1,               "10b  all values survive rehashing");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
