/* bugs/006-map-missing-key.cy
 *
 * Demonstrates the two new Map lookup options:
 *   1. Get(K)   – throws KeyException on missing key (new default)
 *   2. TryGet(K, V*) – fast bool check, never throws
 */

#include <stdio.h>
#include "map.h"

int main() {
    printf("=== Map missing-key tests ===\n");

    Map<String,int>* m = new Map<String,int>();
    defer delete m;

    m.Set("exists", 42);

    /* Option 1: Get throws */
    try {
        int v = m->Get("missing");
        printf("ERROR: Get survived (bad)\n");
    } catch (e) {
        printf("CAUGHT Get KeyException: %s\n", e.msg);
    }

    /* Option 2: TryGet fast bool path */
    int out;
    if (m->TryGet("exists", &out))
        printf("TryGet(exists) → %d\n", out);
    else
        printf("TryGet(exists) failed (bad)\n");

    if (!m->TryGet("missing", &out))
        printf("TryGet(missing) correctly returned false\n");
    else
        printf("TryGet(missing) wrongly succeeded\n");

    return 0;
}
