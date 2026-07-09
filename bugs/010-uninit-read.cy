/* bugs/010-uninit-read.cy
 *
 * C99/C11 definite-assignment violation: reading a local scalar before any
 * definite initialization is UB.  The ownership pass (or a future definite-
 * assignment data-flow check) can emit a compile-time warning/error for the
 * obvious cases, or the runtime guard can detect it when the value is used in
 * a pointer context (wild pointer) or as a divisor etc.
 *
 * This bug file demonstrates the classic pattern that we want to catch:
 * an uninitialized pointer dereference.
 *
 * Run:
 *   ./bin/classyc -g -fexceptions -I include bugs/010-uninit-read.cy -eg
 */
#include <stdio.h>

int main() {
    printf("=== uninitialized-read safety ===\n");

    int caught = 0;

    int *p;                 /* deliberately uninitialized */
    try {
        int v = *p;         /* wild read */
        printf("FAIL: read uninit pointer, got %d\n", v);
    } catch (Exception e) {
        printf("PASS: caught wild uninit deref: %s\n", e.msg);
        caught++;
    }

    if (caught == 1) {
        printf("Uninitialized pointer dereference caught.\n");
        return 0;
    }
    return 1;
}
