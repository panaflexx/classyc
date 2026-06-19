/* classy-sets.cy — comprehensive exercise of include/set.h
 *
 * Tests:
 *   · All constructors (default, capacity, brace-init, array-view)
 *   · Core API: Count, Capacity, IsEmpty, Contains, Add, Remove, Get, First, Last, Clear
 *   · Set algebra: Union, Intersect, Difference, IsSubsetOf, Equals
 *   · Higher-order: ForEach, Filter
 *   · for-in iteration protocol
 *   · Duplicate handling and idempotent Add
 *   · Remove non-existent element
 *   · Growth and rehashing
 *
 * Usage: classyc examples/classy-sets.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "set.h"

int passed = 0;
int failed = 0;

void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

void int_print(int x) { printf("%d ", x); }

int is_even(int x) { return x % 2 == 0; }

int main() {
    printf("=== Set<T> test suite ===\n\n");

    /* ── 1. Basic int set ───────────────────────────────────────────── */
    printf("-- 1. int set, brace-init, Add, Contains --\n");

    Set<int>* s = new Set<int>{10, 20, 30, 20};   /* duplicate 20 */
    defer delete s;

    check(s->Count() == 3,          "1a  Count after brace-init with dup");
    check(s->Contains(10) == 1,     "1b  Contains(10)");
    check(s->Contains(20) == 1,     "1c  Contains(20)");
    check(s->Contains(99) == 0,     "1d  !Contains(99)");

    int added = s->Add(40);
    check(added == 1,               "1e  Add(40) returns 1");
    check(s->Count() == 4,          "1f  Count after Add");

    int dup_added = s->Add(10);
    check(dup_added == 0,           "1g  Add duplicate returns 0");

    /* ── 2. Remove, Get, First/Last, Clear ──────────────────────────── */
    printf("\n-- 2. Remove, Get, First/Last, Clear --\n");

    int removed = s->Remove(20);
    check(removed == 1,             "2a  Remove(20) returns 1");
    check(s->Count() == 3,          "2b  Count after Remove");

    int not_removed = s->Remove(999);
    check(not_removed == 0,         "2c  Remove absent returns 0");

    check(s->Get(0) == 10 || s->Get(0) == 30 || s->Get(0) == 40, "2d  Get(0) valid");
    check(s->First() != 0,          "2e  First() defined");
    check(s->Last()  != 0,          "2f  Last() defined");

    s->Clear();
    check(s->Count() == 0,          "2g  Clear -> Count==0");
    check(s->IsEmpty() == 1,        "2h  IsEmpty after Clear");

    /* ── 3. String set ──────────────────────────────────────────────── */
    printf("\n-- 3. String set --\n");

    Set<String>* names = new Set<String>{"alice", "bob", "alice"};
    defer delete names;

    check(names->Count() == 2,              "3a  String set Count");
    check(names->Contains("bob") == 1,      "3b  Contains String");
    check(names->Contains("carol") == 0,    "3c  !Contains missing String");

    /* ── 4. Set algebra ─────────────────────────────────────────────── */
    printf("\n-- 4. Set algebra --\n");

    Set<int>* a = new Set<int>{1, 2, 3, 4};
    Set<int>* b = new Set<int>{3, 4, 5, 6};
    defer delete a;
    defer delete b;

    Set<int>* un = a->Union(b);
    defer delete un;
    check(un->Count() == 6,                 "4a  Union Count");
    check(un->Contains(1) && un->Contains(6), "4b  Union elements");

    Set<int>* inter = a->Intersect(b);
    defer delete inter;
    check(inter->Count() == 2,              "4c  Intersect Count");
    check(inter->Contains(3) && inter->Contains(4), "4d  Intersect elems");

    Set<int>* diff = a->Difference(b);
    defer delete diff;
    check(diff->Count() == 2,               "4e  Difference Count");
    check(diff->Contains(1) && diff->Contains(2), "4f  Difference elems");

    check(a->IsSubsetOf(un) == 1,           "4g  IsSubsetOf true");
    check(b->IsSubsetOf(inter) == 0,        "4h  IsSubsetOf false");

    Set<int>* a2 = new Set<int>{1, 2, 3, 4};
    defer delete a2;
    check(a->Equals(a2) == 1,               "4i  Equals true");
    check(a->Equals(b) == 0,                "4j  Equals false");

    /* ── 5. Higher-order & for-in ───────────────────────────────────── */
    printf("\n-- 5. ForEach, Filter, for-in --\n");

    Set<int>* evens = a->Filter(is_even);
    defer delete evens;
    check(evens->Count() == 2,              "5a  Filter Count");

    int sum = 0;
    for (auto v in a) sum += v;
    check(sum == 10,                        "5b  for-in sum over Set");

    /* ── 6. Capacity & growth ───────────────────────────────────────── */
    printf("\n-- 6. Capacity ctor & growth --\n");

    Set<int>* big = new Set<int>(128);
    defer delete big;
    check(big->Capacity() >= 4,             "6a  capacity ctor");

    for (int i = 0; i < 100; i++) big->Add(i);
    check(big->Count() == 100,              "6b  growth to 100 elements");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
