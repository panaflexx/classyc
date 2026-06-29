/* Generic functions — type inference at call sites.
   Foundation for sort/map/reduce/hash/equality utilities. */

#include <list.h>
#include <map.h>

/* The canonical example: a max over any orderable T. */
T Max<T>(T a, T b) {
    return a > b ? a : b;
}

/* A two-parameter generic: pair construction.  K from arg 0, V from arg 1. */
K First<K, V>(K k, V v) {
    return k;
}

V Second<K, V>(K k, V v) {
    return v;
}

int main() {
    int failures = 0;

    /* T=int inferred from integer literals. */
    auto m = Max(3, 5);
    printf("Max(3, 5) = %d\n", m);
    if (m != 5) { printf("FAIL: Max(3,5) expected 5 got %d\n", m); failures++; }

    /* T=double inferred from double literals. */
    auto d = Max(1.5, 2.5);
    printf("Max(1.5, 2.5) = %g\n", d);
    if (d != 2.5) { printf("FAIL: Max(1.5,2.5) expected 2.5 got %g\n", d); failures++; }

    /* Two-parameter generic: K=int, V=String. */
    auto f = First(1, "hello");
    printf("First(1, \"hello\") = %d\n", f);
    if (f != 1) { printf("FAIL: First(1,hello) expected 1 got %d\n", f); failures++; }

    auto sc = Second(1, "hello");
    printf("Second(1, \"hello\") = %s\n", sc);
    if (!sc.equals("hello")) { printf("FAIL: Second(1,hello) expected hello got %s\n", sc); failures++; }

    /* Reuse: a second call with the same inferred types must hit the cache
       (no duplicate specialization). */
    auto m2 = Max(10, 20);
    if (m2 != 20) { printf("FAIL: Max(10,20) expected 20 got %d\n", m2); failures++; }

    if (failures == 0) printf("PASS\n");
    else printf("FAIL (%d assertions)\n", failures);
    return failures;
}
