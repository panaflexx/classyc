/* val-028-list-fixes.cy — validates List<T> fixes:
 *  - Get/First/Last/Pop/RemoveAt/Set trap OutOfBoundsException (not UB/no-op)
 *  - GetOr/TryGet/FirstOr/LastOr safe fallbacks
 *  - Clear/RemoveAt/Set properly destroy old elements (dtor counting for owned ptrs)
 *  - owns() chaining + owns(int) variant unified (was void vs List* conflict)
 *  - New APIs: Where, Select, Any, All, Find, FindOr, AddRange, InsertRange,
 *              Distinct, Repeat, ToArrayDict, ToString, FindIndex
 *  - ToArray uses assignment copy (not memcpy)
 */

#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int is_even(int x) { return x % 2 == 0; }
int is_pos(int x)  { return x > 0; }
int gt_10(int x)   { return x > 10; }
int times2(int x)  { return x * 2; }

int dtor_count = 0;
class Counter {
    int id;
    Counter(int id) { this->id = id; }
    ~Counter() { dtor_count++; }
    int getId() { return this->id; }
};

int main() {
    printf("=== val-028 List<T> fixes ===\n\n");

    /* ── 1. Traps ─────────────────────────────────────────────────────── */
    printf("-- 1. OOB traps --\n");
    List<int>* xs = new List<int>{ 10, 20, 30 };
    defer delete xs;

    int caught = 0;
    try { int _v = xs->Get(5); (void)_v; } catch (e) { caught = 1; }
    check(caught == 1, "1a  Get(5) throws OutOfBoundsException");

    caught = 0;
    try { int _v2 = xs->Get(-1); (void)_v2; } catch (e) { caught = 1; }
    check(caught == 1, "1b  Get(-1) throws");

    List<int>* empty = new List<int>();
    defer delete empty;

    caught = 0;
    try { int _v3 = empty->First(); (void)_v3; } catch (e) { caught = 1; }
    check(caught == 1, "1c  First() on empty throws");

    caught = 0;
    try { int _v4 = empty->Last(); (void)_v4; } catch (e) { caught = 1; }
    check(caught == 1, "1d  Last() on empty throws");

    caught = 0;
    try { int _v5 = empty->Pop(); (void)_v5; } catch (e) { caught = 1; }
    check(caught == 1, "1e  Pop() on empty throws");

    caught = 0;
    try { xs->RemoveAt(10); } catch (e) { caught = 1; }
    check(caught == 1, "1f  RemoveAt(10) throws");

    caught = 0;
    try { xs->Set(10, 99); } catch (e) { caught = 1; }
    check(caught == 1, "1g  Set(10, v) throws");

    caught = 0;
    try { empty->Set(0, 1); } catch (e) { caught = 1; }
    check(caught == 1, "1h  Set(0,v) on empty throws");

    /* ── 2. Safe fallbacks ─────────────────────────────────────────── */
    printf("\n-- 2. Safe fallbacks --\n");
    check(xs->GetOr(1, -1) == 20, "2a  GetOr present returns value");
    check(xs->GetOr(10, -1) == -1, "2b  GetOr OOB returns fallback");
    check(xs->GetOr(-1, 99) == 99, "2c  GetOr negative returns fallback");

    int out = 0;
    check(xs->TryGet(0, &out) == 1 && out == 10, "2d  TryGet present -> true + value");
    check(xs->TryGet(99, &out) == 0, "2e  TryGet OOB -> false");
    check(xs->TryGet(1, NULL) == 0, "2f  TryGet NULL out -> false");

    check(empty->FirstOr(-7) == -7, "2g  FirstOr empty returns fallback");
    check(empty->LastOr(-8) == -8,  "2h  LastOr empty returns fallback");
    check(xs->FirstOr(-1) == 10,    "2i  FirstOr non-empty returns first");
    check(xs->LastOr(-1) == 30,     "2j  LastOr non-empty returns last");

    /* ── 3. Mutation destroys old owned pointers ────────────────────── */
    printf("\n-- 3. Mutation destroys old owned elements --\n");

    {
        dtor_count = 0;
        List<Counter*>* owned = new List<Counter*>().owns();
        owned->Add(new Counter(1));
        owned->Add(new Counter(2));
        owned->Add(new Counter(3));
        int before = dtor_count;
        owned->RemoveAt(1);
        check(owned->Count() == 2, "3a  RemoveAt shrinks Count");
        check(dtor_count - before == 1, "3b  RemoveAt ran owned element dtor");
        int before2 = dtor_count;
        owned->Clear();
        check(dtor_count - before2 == 2, "3c  Clear() ran remaining owned dtors");
        delete owned;
    }

    {
        dtor_count = 0;
        List<Counter*>* owned = new List<Counter*>().owns();
        owned->Add(new Counter(10));
        owned->Add(new Counter(20));
        int before = dtor_count;
        owned->Set(0, new Counter(99));
        check(dtor_count - before == 1, "3d  Set() destroys replaced owned element");
        delete owned;
    }

    {
        dtor_count = 0;
        List<Counter*>* c = new List<Counter*>().owns();
        c->Add(new Counter(1));
        c->Add(new Counter(2));
        int before = dtor_count;
        c->Clear();
        check(dtor_count - before == 2, "3e  Clear() on owning list frees all");
        check(c->Count() == 0, "3f  Clear() sets Count=0");
        delete c;
    }

    /* ── 4. owns() API ─────────────────────────────────────────────── */
    printf("\n-- 4. owns() API --\n");
    {
        List<int>* a = new List<int>().owns(0);
        check(a != NULL, "4a  owns(0) chainable");
        List<int>* b = a->owns();
        check(b == a, "4b  owns() chaining returns this");
        delete a;
    }

    /* ── 5. New ergonomic APIs ─────────────────────────────────────── */
    printf("\n-- 5. New ergonomic APIs --\n");

    List<int>* nums = new List<int>{ 1, 2, 3, 4, 5, 6 };
    defer delete nums;

    List<int>* wh = nums->Where(is_even);
    defer delete wh;
    check(wh->Count() == 3 && wh->Get(0) == 2, "5a  Where(even) filters");

    List<int>* sel = nums->Select(times2);
    defer delete sel;
    check(sel->Get(0) == 2 && sel->Get(5) == 12, "5b  Select(x*2) maps");

    check(nums->Any(is_even) == 1 && nums->All(is_pos) == 1, "5c  Any/All predicates");
    check(nums->Any(gt_10) == 0, "5d  Any(gt_10) false on [1..6]");

    int found = nums->Find(is_even);
    check(found == 2, "5e  Find(even) returns first match");
    check(nums->Find(gt_10) == 0, "5f  Find(no match) returns zero-init");
    check(nums->FindOr(-1, gt_10) == -1, "5g  FindOr no match returns fallback");
    check(nums->FindOr(-1, is_even) == 2, "5h  FindOr match returns first");

    check(nums->FindIndex(is_even) == 1, "5i  FindIndex(even) == 1");

    List<int>* src = new List<int>{ 10, 20 };
    defer delete src;
    List<int>* dst = new List<int>{ 1, 2, 3 };
    defer delete dst;
    dst->AddRange(src);
    check(dst->Count() == 5 && dst->Get(3) == 10, "5j  AddRange appends all");

    List<int>* base = new List<int>{ 1, 2, 5 };
    defer delete base;
    List<int>* ins = new List<int>{ 3, 4 };
    defer delete ins;
    base->InsertRange(2, ins);
    check(base->Count() == 5 && base->Get(2) == 3 && base->Get(4) == 5, "5k  InsertRange inserts at index");

    List<int>* dup = new List<int>{ 1, 2, 2, 3, 1, 4, 2 };
    defer delete dup;
    List<int>* uniq = dup->Distinct();
    defer delete uniq;
    check(uniq->Count() == 4, "5l  Distinct removes duplicates");
    check(uniq->Contains(1) && uniq->Contains(4), "5m  Distinct preserves values");

    List<int>* rep = List<int>.Repeat(42, 3);
    defer delete rep;
    check(rep->Count() == 3 && rep->Get(1) == 42, "5n  Repeat(42,3)");

    dict arrDict = nums->ToArrayDict();
    check((int)arrDict.length() == 6, "5o  ToArrayDict() alias works");

    List<int>* jt = new List<int>{ 1, 2, 3 };
    defer delete jt;
    String j = jt->ToString();
    check(strcmp(j, "[1,2,3]") == 0, "5p  ToString() returns JSON array string");

    /* String content equality (not pointer ==) for Contains/IndexOf/Distinct/Equals */
    List<String>* tags = new List<String>();
    defer delete tags;
    tags->Add("AURORA");
    tags->Add("RIVEN");
    tags->Add("AURORA");
    check(tags->Contains("AURORA") == 1, "5q  List<String>.Contains literal by content");
    check(tags->Contains("NOPE") == 0,   "5r  Contains miss");
    check(tags->IndexOf("RIVEN") == 1,   "5s  IndexOf content");
    List<String>* utags = tags->Distinct();
    defer delete utags;
    check(utags->Count() == 2,            "5t  Distinct String by content");
    List<String>* t2 = new List<String>{"AURORA", "RIVEN"};
    defer delete t2;
    check(utags->Equals(t2) == 1,         "5u  Equals String lists by content");

    /* ── 6. Copy correctness ──────────────────────────────────────── */
    printf("\n-- 6. Copy correctness --\n");
    List<int>* orig = new List<int>{ 7, 8, 9 };
    defer delete orig;
    int* carr = orig->ToArray();
    check(carr != NULL && carr[0] == 7 && carr[2] == 9, "6a  ToArray copies correctly");
    carr[0] = 99;
    check(orig->Get(0) == 7, "6b  ToArray is independent copy");
    free(carr);

    List<int>* src2 = new List<int>{ 10, 20, 30 };
    defer delete src2;
    int dest[3] = { 0, 0, 0 };
    src2->CopyTo(dest);
    check(dest[0] == 10 && dest[2] == 30, "6c  CopyTo copies elements");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
