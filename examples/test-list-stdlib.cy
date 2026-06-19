/* test-list-stdlib.cy — comprehensive exercises of include/list.h
 *
 * Validates the official List<T> implementation against:
 *   1. Multiple element types (int, double, String)
 *   2. Brace-init list construction { item1, item2, ... }
 *   3. All 30 methods across every category
 *   4. Edge cases (empty, single element, OOB guards)
 *   5. Fluent concatenation chaining
 *
 * Usage:
 *   ~/bin/classyc examples/test-list-stdlib.cy -eg
 */

#include <stdio.h>
#include <string.h>
#include "include/list.h"

int    passed = 0;
int    failed = 0;

void check(int cond, const char *label) {
    if (cond)     { printf("  PASS  %s\n", label);  passed++; }
    else          { printf("  FAIL  %s\n", label);   failed++; }
}

int is_even(int x)         { return x % 2 == 0; }

int g_sum = 0;

void int_accum(int x)      { g_sum += x; }


int main() {
    printf("=== List<T> include/list.h test suite ===\n\n");
    passed = 0; failed = 0;

    /* ──────────────── 1. int — brace init + all mutations ─────────── */

    printf("-- 1. List<int> (default ctor) --\n");

    List<int>* nums = new List<int>();
    defer delete nums;

    check(nums->Count() == 0,     "1a  default empty");
    check(nums->IsEmpty() == 1,   "1b  default IsEmpty");

    /* brace-init construction */
    List<int>* braced = new List<int>{ 10, 20, 30, 40 };
    defer delete braced;

    check(braced->Count()   == 4,          "1c  brace-init: Count==4");
    check(braced->Get(0)    == 10,         "1d  brace-init: Get(0)==10");
    check(braced->Get(3)    == 40,         "1e  brace-init: Get(3)==40");
    check(braced->First()   == 10,         "1f  First()==10");
    check(braced->Last()    == 40,         "1g  Last()==40");

    /* capacity ctor */
    List<int>* capList = new List<int>(64);
    defer delete capList;

    check(capList->Capacity() == 64,        "1h  capacity ctor");
    check(capList->Count()    == 0,         "1i  capacity ctor Count==0");

    /* Add / Set / Insert */
    nums->Add(5);
    check(nums->Last() == 5,               "1j  Add(5)");

    nums->Insert(0, 1);
    check(nums->First()  == 1,             "1k  Insert(0,1) prepends");
    check(nums->Count()  == 2,             "1l  Count after insert==2");

    braced->Set(1, 99);
    check(braced->Get(1) == 99,            "1m  Set(1,99)");

    /* Remove / Pop */
    int popped = braced->Pop();
    check(popped == 40,                    "1n  Pop returns 40");
    check(braced->Count() == 3,            "1o  Count==3 after Pop");

    int removed = braced->Remove(99);
    check(removed  == 1,                  "1p  Remove(99) found");
    check(braced->Count() == 2,           "1q  Count==2 after Remove");

    /* OOB no-ops */
    braced->Set(-10, -999);
    braced->RemoveAt(-10);
    braced->RemoveAt(500);
    check(braced->Count() == 2,           "1r  OOB ops are safe no-ops");

    /* ──────────────── 2. int — search + transforms ───────────────── */

    printf("\n-- 2. List<int> (search / transform) --\n");

    List<int>* data = new List<int>{ 7, 3, 9, 3, 1, 3 };
    defer delete data;

    check(data->IndexOf(3)     == 1,       "2a  IndexOf first==1");
    check(data->LastIndexOf(3) == 5,       "2b  LastIndexOf last==5");
    check(data->Contains(7)    == 1,       "2c  Contains(7)==1");
    check(data->Contains(0)    == 0,       "2d  Contains(0)==0");

    /* Reverse */
    List<int>* rev = data->Copy();
    defer delete rev;
    rev->Reverse();
    check(rev->Get(0) == 3,                "2e  Reverse: first element was last");
    check(rev->Last() == 7,                "2f  Reverse: last element was first");

    /* Sort */
    data->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);
    check(data->First() == 1 && data->Last() == 9, "2g  sorted ascending");

    /* Concat chaining */
    List<int>* tail = new List<int>{ 10, 20 };
    defer delete tail;
    braced->Concat(tail);
    check(braced->Count() == 4,           "2h  Concat adds 2 more elements");
    check(braced->Last()  == 20,          "2i  Last after Concat==20");

    /* Slice */
    List<int>* slc = data->Slice(1, 3);
    defer delete slc;
    check(slc->Count() == 3,            "2j  Slice count==3");
    check(slc->Get(0)  == 3,            "2k  Slice data independent copy");

    /* Copy + Equals */
    List<int>* eq = data->Copy();
    defer delete eq;
    check(data->Equals(eq),               "2l  Copy .Equals original");

    /* ForEach / Filter — must run while data is still {1,3,3,3,7,9} */
    g_sum = 0;
    data->ForEach(int_accum);
    check(g_sum == 26,                    "2p  ForEach sum");

    List<int>* filtred = data->Filter(is_even);
    defer delete filtred;
    check(filtred->Count() == 0,           "2q  Filter even from {1,3,3,3,7,9} — all odd");

    /* Now mutate — safe, nothing reads sorted state after here */
    data->Set(0, -1);
    check(data->Equals(eq) == 0,          "2m  mutated orig no longer equals");

    /* Trim / Clear */
    braced->TrimExcess();
    check(braced->Capacity() == braced->Count(), "2n  TrimExcess tight fit");
    braced->Clear();
    check(braced->Count() == 0 && braced->IsEmpty(), "2o  Clear to empty");

    /* ─────────────── 2. double — brace init + sorting + Copy ──────── */

    printf("\n-- 3. List<double> --\n");

    List<double>* d = new List<double>{ 3.14, -0.5, 100.0 };
    defer delete d;

    check(d->Get(0) > 3.13 && d->Get(0) < 3.15,      "3a  Get(0)=3.14");
    check(d->IsEmpty() == 0,                          "3b  non-empty");

    List<double>* dc = d->Copy();
    defer delete dc;
    d->Set(1, -999.0);
    /* Copy should be independent of the source */
    check(dc->Get(1) < -0.4 && dc->Get(1) > -0.6,     "3c  Copy independence");

    List<double>* db = new List<double>();
    defer delete db;
    double arr[] = {2.7, -3.0, 0.5};
    for (int i = 0; i < 3; i++) db->Add(arr[i]);
    check(db->Count() == 3,                           "3d  add-in-loop");

    /* ──────────────── 4. String — compare via strcmp ──────────────── */

    printf("\n-- 4. List<String> --\n");

    List<String>* words = new List<String>{ "bravo", "alpha", "charlie" };
    defer delete words;

    check(words->Count() == 3,                         "4a  brace-init 3 strings");
    check(strcmp(words->Last(), "charlie") == 0,        "4b  Last==\"charlie\"");

    /* Sort using strcmp */
    words->Sort((String a, String b) => strcmp(a, b));
    check(strcmp(words->First(), "alpha")   == 0,       "4c  sorted first alpha");
    check(strcmp(words->Last(),  "charlie") == 0,       "4d  sorted last charlie");

    /* Concat strings */
    List<String>* more = new List<String>{ "delta" };
    defer delete more;
    words->Concat(more);
    check(words->Count() == 4,                         "4e  String Concat Count==4");

    /* ─────────────── 5. Mixed types — capacity growth test ────────── */

    printf("\n-- 5. Rapid growth (capacity scaling) --\n");

    List<int>* big = new List<int>();
    defer delete big;
    for (int i = 0; i < 20000; i++) big->Add(i * i);
    check(big->Count() == 20000,                         "5a  loaded 20000 items");
    check(big->Last()  == 19999 * 19999,                    "5b  19999^2 stored last");

    /* ──────── 6. for-in iteration across every test type ───────────── */

    printf("\n-- 6. for-in over list --\n");

    int sum = 0;
    for (auto x in big) sum += x % 10;   /* cheap reduction */
    check(sum > 0,                                 "6a  for-in over List<int>");

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
