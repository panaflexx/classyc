/* val-007-list.cy — validates the generic List<T> (include/list.h).
 *
 * Correct idiom (NOT the README's `List<int> x = {..}; x.Filter().Map()`):
 *   List<int>* xs = new List<int>{ 1, 2, 3 };   // heap, brace-init
 *   xs->Filter((int x) => x > 1);               // arrow calls; Filter exists
 *   defer delete xs;
 * There is NO `.Map` method on List<T> (see SHORTCOMINGS.md B4). For array
 * pipelines use the lowercase seq methods `.filter/.map/.reduce/.ToList`.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-007-list.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int g_sum = 0;
void accum(int x) { g_sum += x; }

int main() {
    printf("=== val-007 List<T> ===\n\n");

    /* brace-init + accessors */
    List<int>* nums = new List<int>{ 1, 2, 3, 4, 5, 6, 7, 8 };
    defer delete nums;
    check(nums->Count() == 8,   "brace-init Count");
    check(nums->Get(0) == 1,    "Get(0)");
    check(nums->First() == 1 && nums->Last() == 8, "First/Last");

    /* mutation */
    nums->Add(9);
    check(nums->Count() == 9 && nums->Last() == 9, "Add appends");
    nums->Set(0, 100);
    check(nums->Get(0) == 100,  "Set replaces");
    check(nums->Pop() == 9,     "Pop returns last");
    nums->RemoveAt(0);
    check(nums->Get(0) == 2,    "RemoveAt shifts");

    /* search */
    check(nums->IndexOf(5) >= 0, "IndexOf present");
    check(nums->Contains(5) == 1 && nums->Contains(999) == 0, "Contains");

    /* Filter (returns new heap list) */
    List<int>* src = new List<int>{ -2, 5, -8, 3, -1, 9, 4 };
    defer delete src;
    List<int>* pos = src->Filter((int x) => x > 0);
    defer delete pos;
    check(pos->Count() == 4,    "Filter keeps matching");

    /* Sort in place */
    pos->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);
    check(pos->Get(0) == 3 && pos->Last() == 9, "Sort ascending");

    /* ForEach */
    g_sum = 0;
    pos->ForEach(accum);
    check(g_sum == 3 + 4 + 5 + 9, "ForEach visits all");

    /* Reverse / Copy / Equals / Slice / Concat */
    List<int>* a = new List<int>{ 1, 2, 3 };
    defer delete a;
    List<int>* c = a->Copy();
    defer delete c;
    check(a->Equals(c) == 1,    "Copy + Equals");
    c->Reverse();
    check(c->Get(0) == 3,       "Reverse in place");
    List<int>* sl = a->Slice(1, 2);
    defer delete sl;
    check(sl->Count() == 2 && sl->Get(0) == 2, "Slice range");

    /* String list + Filter */
    List<String>* fruit = new List<String>{ "apple", "banana", "avocado", "cherry" };
    defer delete fruit;
    List<String>* aw = fruit->Filter((String s) => ((char*)s)[0] == 'a');
    defer delete aw;
    check(aw->Count() == 2,     "String Filter by predicate");
    check(strcmp(aw->Get(0), "apple") == 0, "String Filter element 0");

    /* C array -> List<T> via .ToList() (length threads in via items.count()) */
    String names[3] = { "alice", "bob", "carol" };
    auto l = names.ToList();
    check(l->Count() == 3,      "array.ToList() recovers length");
    check(strcmp(l->Get(2), "carol") == 0, "ToList element preserved");

    /* slice pipeline: lowercase seq methods on a C array */
    int raw[6] = { 1, 2, 3, 4, 5, 6 };
    auto evens = raw.filter((int x) => x % 2 == 0).ToList();
    check(evens->Count() == 3 && evens->Get(0) == 2, "array.filter().ToList() slice pipeline");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
