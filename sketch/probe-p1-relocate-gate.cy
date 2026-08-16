/* probe-p1-relocate-gate.cy — class with a RESOURCE-FREEING dtor stored
 * by value in List<T> (BY-VALUE.md P1).
 *
 * Expected if P1 gate existed: COMPILE ERROR ("not marked [[copyable_no_release]],
 * use List<Owns*>.owns()").
 *
 * Without the gate this compiles, and then List's bitwise relocations /
 * copies (Add temp, Get out-copy) alias the same malloc'd pointer in two
 * slots → use-after-free / double-free at runtime.
 *
 * Run: ./bin/classyc -g -I include sketch/probe-p1-relocate-gate.cy -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include "list.h"

int frees = 0;
class Owns {
    char* p;
    Owns(int v) {
        p = (char*)malloc(16);
        snprintf(p, 16, "v%d", v);
    }
    ~Owns() { frees++; free(p); p = NULL; }
    const char* str() { return p; }
};

int main() {
    printf("=== probe P1: non-relocatable element in by-value List ===\n\n");

    /* If the compiler accepts this, the P1 safety gate is missing. */
    auto xs = List<Owns>();
    for (int i = 0; i < 4; i++)
        xs.Add(Owns(i));

    /* Get() copies the element OUT by value; the out-copy's dtor frees p
     * while the list slot still aliases it. */
    for (int i = 0; i < xs.Count(); i++) {
        Owns copy = xs.Get(i);
        printf("  elem %d = %s\n", i, copy.str());
    }

    printf("  frees so far: %d (list still holds %d elements)\n",
           frees, xs.Count());

    /* Scope exit: ~List destroys each element again -> double free. */
    printf("survived to scope exit\n");
    return 0;
}
