/* val-050-nested-collections.cy — List<List<T>> and Map<G, List<V>> by value.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-050-nested-collections.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}
int parity(int x) { return x & 1; }

int main(void) {
    printf("=== val-050 nested collections ===\n\n");

    printf("-- 1. List<List<int>> --\n");
    {
        auto sheet = List<List<int>>();
        auto r0 = List<int>();
        r0.Add(1); r0.Add(2); r0.Add(3);
        sheet.Add(move r0);
        auto r1 = List<int>();
        r1.Add(10); r1.Add(20);
        sheet.Add(move r1);
        check(sheet.Count() == 2, "1a two rows");
        check(sheet.Get(0).Count() == 3 && sheet.Get(0).Get(1) == 2, "1b Get row copy");
        sheet.GetMut(0)->Add(4);
        check(sheet.Get(0).Count() == 4, "1c GetMut append");
        check(sheet.Get(1).Get(0) == 10, "1d second row intact");
    }

    printf("\n-- 2. Map<int, List<int>> --\n");
    {
        auto m = Map<int, List<int>>();
        auto b = List<int>();
        b.Add(7); b.Add(8);
        m.Set(1, move b);
        check(m.Count() == 1 && m.Get(1).Count() == 2, "2a Set/Get");
        m.GetMut(1)->Add(9);
        check(m.Get(1).Count() == 3 && m.Get(1).Get(2) == 9, "2b GetMut");
    }

    printf("\n-- 3. GroupBy Phase B --\n");
    {
        auto nums = List<int>();
        for (int i = 1; i <= 6; i++) nums.Add(i);
        auto g = nums.GroupBy(parity);
        check(g.Count() == 2, "3a two buckets");
        check(g.Get(0).Count() == 3 && g.Get(1).Count() == 3, "3b sizes");
        check(g.Get(0).Get(0) == 2, "3c even first");
        int seen = 0;
        for (auto k, bucket in g) {
            (void)k;
            seen += bucket.Count();
        }
        check(seen == 6, "3d for-in");
    }

    printf("\n-- 4. List* [] sugar still Get --\n");
    {
        List<char*>* strs = new List<char*>();
        defer delete strs;
        strs->Add("hello");
        check(strcmp(strs[0], "hello") == 0, "4a heap List* [i] is Get");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
