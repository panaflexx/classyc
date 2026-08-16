/* probe-p1b-transform-doublefree.cy — the REAL P1 hazard: a value
 * transform (Where/Copy) bitwise-copies elements into a SECOND owning
 * list.  Both lists' ~List run ~T on aliases of the same resource.
 *
 * Probe A: resource-owning dtor (malloc/free) → expect double-free abort
 *          if the hazard is real.
 * Probe B: the take(T(...)) prvalue-arg leak with a resource-owning type
 *          (leaked malloc, detected via counting allocs vs frees).
 *
 * Run: ./bin/classyc -g -I include sketch/probe-p1b-transform-doublefree.cy -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include "list.h"

int allocs = 0, frees = 0;
class Owns {
    char* p;
    Owns(int v) { p = (char*)malloc(16); snprintf(p, 16, "v%d", v); allocs++; }
    ~Owns() { if (p) { frees++; free(p); p = NULL; } }
    int get() { return p ? (int)p[1] : -1; }
};

int pred(Owns o) { return 1; }

void take(Owns o) { }

int main() {
    printf("=== probe P1b: transform double-free + prvalue leak ===\n\n");

    printf("-- A. Where copy into second owning list --\n");
    {
        auto xs = List<Owns>();
        xs.Add(Owns(1));
        xs.Add(Owns(2));
        auto ys = xs.Where(pred);
        printf("  xs=%d ys=%d allocs=%d frees=%d\n",
               xs.Count(), ys.Count(), allocs, frees);
        /* scope exit: ~ys frees p of 2 elements; ~xs frees the SAME p again */
    }
    printf("  survived double-owner scope exit (allocs=%d frees=%d)\n",
           allocs, frees);

    printf("-- B. take(Owns(9)) prvalue arg --\n");
    {
        int a0 = allocs, f0 = frees;
        take(Owns(9));
        printf("  delta: +allocs=%d +frees=%d  %s\n", allocs-a0, frees-f0,
               (allocs-a0)==(frees-f0) ? "OK" : "*** LEAKED malloc ***");
    }

    printf("\ntotal allocs=%d frees=%d\n", allocs, frees);
    return 0;
}
