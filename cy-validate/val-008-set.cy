/* val-008-set.cy — validates the generic Set<T> (include/set.h), focusing on
 * the README claim: String keys hash/compare BY CONTENT, objects BY IDENTITY.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-008-set.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "set.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Track {
    String title;
    Track(String t) { this->title = t; }
    ~Track() {}
};

int main() {
    printf("=== val-008 Set<T> ===\n\n");

    /* int set: dedup + membership */
    Set<int>* s = new Set<int>{10, 20, 30, 20};
    defer delete s;
    check(s->Count() == 3,        "int dedup on brace-init");
    check(s->Add(40) == 1,        "Add new -> 1");
    check(s->Add(10) == 0,        "Add duplicate -> 0");
    check(s->Contains(20) == 1 && s->Contains(99) == 0, "Contains");
    check(s->Remove(20) == 1 && s->Count() == 3, "Remove");

    /* String set: CONTENT hashing — two distinct char* with same bytes dedup */
    String n1 = "alice";
    String n2 = "alic";
    n2 = n2 + "e";                /* heap-built "alice", different pointer than n1 */
    Set<String>* names = new Set<String>{};
    defer delete names;
    names->Add(n1);
    int added_dup = names->Add(n2);   /* same CONTENT -> must be rejected */
    check(names->Count() == 1,    "String set hashes by CONTENT (distinct ptr, same bytes dedup)");
    check(added_dup == 0,         "Add content-duplicate String -> 0");
    check(names->Contains("alice") == 1, "Contains by content");

    /* Object set: IDENTITY hashing — two Tracks with same title are distinct */
    Track* t1 = new Track("Kashmir");
    Track* t2 = new Track("Kashmir");   /* same title, different identity */
    Set<Track*>* lib = new Set<Track*>{};
    defer delete lib;
    lib->Add(t1);
    lib->Add(t2);
    check(lib->Count() == 2,      "object set hashes by IDENTITY (same title, 2 entries)");
    check(lib->Contains(t1) == 1, "Contains by identity (t1)");
    lib->Add(t1);
    check(lib->Count() == 2,      "re-Add same pointer is a no-op");
    delete t1; delete t2;

    /* set algebra */
    Set<int>* a = new Set<int>{1, 2, 3, 4};
    Set<int>* b = new Set<int>{3, 4, 5, 6};
    defer delete a; defer delete b;
    Set<int>* un = a->Union(b);
    defer delete un;
    check(un->Count() == 6,       "Union cardinality");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
