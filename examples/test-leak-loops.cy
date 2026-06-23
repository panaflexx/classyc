/* test-leak-loops.cy — exercises the CFG worklist's loop fixpoint.
 *
 * These cases the structured analyzer either got wrong (false negative on
 * loop_double_free) or couldn't detect (path-sensitive break leak).
 */

#include <stdio.h>
#include <stdlib.h>

/* ---- LEAK on second iteration ---- the loop frees `p` once, then
 * iterates and `p` is now a stale pointer that gets freed again.
 * The CFG fixpoint catches this because state propagates through the
 * back-edge: after one body iteration state is Released; meeting with
 * the entry's Owned gives MaybeOwned; the second body iteration's free
 * sees MaybeOwned and we get a "double-free risk" diagnostic. */
void loop_double_free(int n) {
    char *p = (char *)malloc(16);
    while (n > 0) {
        free(p);          /* Released after first iter; on the back-edge
                              this becomes a double-free risk on iter 2+ */
        n = n - 1;
    }
}

/* ---- OK ---- per-iteration allocation, paired free.  The CFG
 * correctly tracks `p` becoming Owned on each iter then Released,
 * and the back-edge meets at Released which means no leak. */
void per_iter_alloc(int n) {
    while (n > 0) {
        char *p = (char *)malloc(16);
        p[0] = 'h';
        free(p);
        n = n - 1;
    }
}

/* ---- LEAK on break-exit ---- the break jumps out while `p` is Owned. */
void break_leak(int cond) {
    char *p = (char *)malloc(16);
    while (1) {
        if (cond) break;        /* exits with p still Owned */
        free(p);
        p = (char *)malloc(16); /* re-acquire */
    }
    /* leak: if break taken, p never freed */
}

/* ---- OK ---- early return inside loop, transferred ownership. */
char *find_thing(int n) {
    char *p = (char *)malloc(16);
    while (n > 0) {
        if (n == 1) return p;   /* transfer to caller */
        n = n - 1;
    }
    free(p);
    return (void *)0;
}

int main() {
    /* loop_double_free(0);   // would underflow into the loop body if called with > 0 */
    per_iter_alloc(3);
    /* break_leak(1);          // intentional runtime leak if called */
    char *e = find_thing(1);
    if (e) free(e);
    return 0;
}
