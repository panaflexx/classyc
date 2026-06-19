/* test-array-to-list.cy — array/slice .ToList() lowering + List<T>(T*) ctor
 *
 * `names.ToList()` is lowered by the compiler to a heap-allocated List<T>
 * built with the array-view constructor List(T* items); inside that ctor the
 * element count is recovered with items.count() — the compiler threads the
 * array's (statically known) / slice's length alongside the bare T* pointer.
 * `auto lst = names.ToList();` deduces List<T>* (the specialization is
 * materialized on demand).  The explicit `new List<T>(arr, n)` form still
 * works: the single-arg ctor is internally (T*, int).
 */

#include <stdio.h>
#include "include/list.h"

int main() {
    String names[] = { "alice", "bob", "carol", "dave" };

    /* Preferred: let the compiler supply the element base + count. */
    List<String> *lst = names.ToList();
    defer delete lst;
    printf("ToList:  count=%d  second=%s\n", lst->Count(), lst->Get(1));

    /* `auto` deduces List<String>* from names.ToList(). */
    auto auto_lst = names.ToList();
    defer delete auto_lst;
    printf("auto:    count=%d  second=%s\n", auto_lst->Count(), auto_lst->Get(1));

    /* Explicit array-view constructor — equivalent lowering, written by hand. */
    List<String> *lst2 = new List<String>(names, names.count());
    defer delete lst2;
    printf("ctor:    count=%d  second=%s\n", lst2->Count(), lst2->Get(1));

    /* ToList() over a filtered slice (filter returns a slice). */
    int nums[] = { 1, 2, 3, 4, 5, 6 };
    List<int> *evens = nums.filter((int x) => x % 2 == 0).ToList();
    defer delete evens;
    printf("evens:   count=%d  [", evens->Count());
    for (auto v in evens) printf(" %d", v);
    printf(" ]\n");

    return 0;
}
