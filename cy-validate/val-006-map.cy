/* val-006-map.cy — validates the generic Map<K,V> (include/map.h).
 *
 * Subscript (m[k]/m[k]=v) lowering, for-in (keys and key,value), insertion
 * order (KeyAt/ValAt), Contains/GetOr/Remove/Clear, Copy/Merge, String->object
 * mapping, int keys, and growth/rehashing. Memory: `new` + `defer delete`.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-006-map.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Track {
    String title;
    int    seconds;
    Track(String t, int s) { this->title = t; this->seconds = s; }
    ~Track() {}
};

int main() {
    printf("=== val-006 Map<K,V> ===\n\n");

    /* String -> int: Set/Get/update, subscript */
    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;
    ages->Set("Ada", 36);
    ages["Bob"] = 40;                 /* subscript insert */
    ages["Ada"] = ages["Ada"] + 1;    /* subscript read+write */
    check(ages->Count() == 2,         "Count after inserts");
    check(ages->Get("Ada") == 37,     "subscript read+update -> 37");
    check(ages->Contains("Bob"),      "Contains present");
    check(!ages->Contains("Zoe"),     "Contains absent");
    check(ages->ContainsKey("Bob"),   "ContainsKey alias");
    check(ages->TryAdd("Ada", 0) == false, "TryAdd existing is false");
    check(ages->TryAdd("Zoe", 9) == true,  "TryAdd new is true");
    check(ages->Get("Zoe") == 9,      "TryAdd inserted value");
    check(ages->Remove("Zoe") == 1,   "Remove TryAdd key");
    /* Get absent throws KeyException */
    try {
        int z = ages->Get("Zoe");
        (void)z;
        printf("FAIL: Get absent should throw\n");
        failed++;
    } catch (e) {
        check(1, "Get absent throws KeyException");
    }
    check(ages->GetOr("Zoe", -1) == -1, "GetOr absent -> default");

    /* Remove */
    check(ages->Remove("Bob") == 1,   "Remove present -> 1");
    check(ages->Remove("Bob") == 0,   "Remove absent -> 0");
    check(ages->Count() == 1,         "Count after remove");

    /* for-in keys & (key,value), insertion order via KeyAt/ValAt */
    Map<String, int>* s = new Map<String, int>();
    defer delete s;
    s["alpha"] = 10; s["beta"] = 20; s["gamma"] = 30;
    int nk = 0; for (auto k in s) nk++;
    check(nk == 3,                    "for-in (keys) visits all");
    int total = 0, np = 0; for (auto k, v in s) { total += v; np++; }
    check(np == 3 && total == 60,     "for-in (key,value) sums values");
    check(strcmp((char*)s->KeyAt(0), "alpha") == 0, "KeyAt(0) preserves insertion order");
    check(s->ValAt(2) == 30,          "ValAt(2) is third value");

    /* Copy + Merge independence (Copy returns Map by value / RAII) */
    Map<String, int>* base = new Map<String, int>();
    defer delete base;
    base["x"] = 1; base["y"] = 2;
    auto dup = base->Copy();
    dup["z"] = 3;
    check(base->Count() == 2 && dup.Count() == 3, "Copy is independent");
    base->Merge(&dup);
    check(base->Count() == 3 && base["z"] == 3,    "Merge pulls entries across");

    /* int -> String keys */
    Map<int, String>* names = new Map<int, String>();
    defer delete names;
    names[1] = "one"; names[2] = "two";
    check(strcmp((char*)names[2], "two") == 0, "int-keyed subscript read");

    /* String -> object (Track*) */
    Map<String, Track*>* lib = new Map<String, Track*>();
    defer delete lib;
    lib["Kashmir"] = new Track("Kashmir", 508);
    lib["Africa"]  = new Track("Africa",  295);
    Track* k = lib["Kashmir"];
    check(k != NULL && k->seconds == 508, "string->object lookup + field");
	try {
		check(lib["Missing"] == NULL,         "absent object key -> NULL");
	} catch (e) {
		check(1, "Get 'Missing' throws KeyException");
	}
    int secs = 0; for (auto title, trk in lib) secs += trk->seconds;
    check(secs == 508 + 295,              "for-in over object values");
    for (auto title, trk in lib) delete trk;   /* map owns only pointers */

    /* growth / rehashing */
    Map<int, int>* sq = new Map<int, int>();
    defer delete sq;
    for (int i = 0; i < 5000; i++) sq[i] = i * i;
    int ok = 1;
    for (int i = 0; i < 5000; i++) if (sq[i] != i * i) ok = 0;
    check(sq->Count() == 5000 && ok == 1, "5000-entry growth survives rehashing");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
