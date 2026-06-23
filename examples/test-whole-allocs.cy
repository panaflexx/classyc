/* test-whole-allocs.cy — first TU of a 2-file -fcheck-whole-allocs demo.
 *
 *   bin/classyc -fcheck-whole-allocs -fownership-report \
 *               examples/test-whole-allocs.cy examples/test-whole-allocs-2.cy
 *
 * Run with -fauto-release to silently fix the leaks:
 *   bin/classyc -fcheck-whole-allocs -fauto-release \
 *               examples/test-whole-allocs.cy examples/test-whole-allocs-2.cy
 *
 * The two files share a stitched TU under -fcheck-whole-allocs, so every
 * allocation across both files appears in one report.  `consume(...)` is
 * declared with __attribute__((releases)) here and defined in the second
 * file, demonstrating that prototype annotations cross the file boundary.
 */
#include <stdio.h>
#include <stdlib.h>

extern void consume(char *p) __attribute__((releases));

void file1_leaks() {
    char *a = (char *)malloc(16);
    a[0] = 'x';
}

void file1_calls_helper() {
    char *b = (char *)malloc(16);
    consume(b);   /* annotation: ownership transferred to helper */
}
