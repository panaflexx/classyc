/* probe-where-nested.cy — single-Get + move-Add pattern for move-only T
 * (List<List<int>>.Where).  If this compiles and prints correctly, the
 * `T item = Get(i); … result.Add(move item);` rewrite is safe for
 * nested collection elements too.
 */

#include <stdio.h>
#include "list.h"

int sum_of(List<int> xs) {
    int s = 0;
    for (int i = 0; i < xs.Count(); i++) s += xs.Get(i);
    return s;
}

int big(List<int> xs) { return xs.Count() >= 2; }

int main() {
    auto outer = List<List<int>>();
    auto a = List<int>(); a.Add(1); a.Add(2);
    auto b = List<int>(); b.Add(9);
    auto c = List<int>(); c.Add(3); c.Add(4); c.Add(5);
    outer.Add(move a);
    outer.Add(move b);
    outer.Add(move c);

    /* single-Get + move-Add pattern */
    auto result = List<List<int>>();
    for (int i = 0; i < outer.Count(); i++) {
        List<int> item = outer.Get(i);      /* deep Copy via Get */
        if (big(item)) result.Add(move item);
    }

    printf("kept=%d (expect 2)\n", result.Count());
    printf("sums=%d,%d (expect 3,12)\n", sum_of(result.Get(0)), sum_of(result.Get(1)));

    /* source must be intact */
    int total = 0;
    for (int i = 0; i < outer.Count(); i++) total += sum_of(outer.Get(i));
    printf("outer intact total=%d (expect %d)\n", total, 3+9+12);
    return 0;
}
