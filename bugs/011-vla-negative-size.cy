/* bugs/011-vla-negative-size.cy
 *
 * C99/C11 §6.7.5.2: Variable-length array size must be positive.
 * Negative or zero size is undefined (commonly SIGSEGV or huge allocation).
 * ClassyC can catch this at the allocation site (before alloca) and turn it
 * into a clean RuntimeException.
 *
 * Run:
 *   ./bin/classyc -g -fexceptions -I include bugs/011-vla-negative-size.cy -eg
 */
#include <stdio.h>

int main() {
    printf("=== VLA negative-size safety ===\n");

    int n = -5;
    int caught = 0;

    try {
        int arr[n];          /* VLA with negative size */
        arr[0] = 42;
        printf("FAIL: VLA(-5) succeeded\n");
    } catch (Exception e) {
        printf("PASS: caught negative VLA size: %s\n", e.msg);
        caught++;
    }

    if (caught == 1) {
        printf("Negative VLA size caught.\n");
        return 0;
    }
    return 1;
}
