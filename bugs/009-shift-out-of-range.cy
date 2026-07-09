/* bugs/009-shift-out-of-range.cy
 *
 * C11 §6.5.7/3: Shift amount must be in [0, width-1].  Negative or >= width
 * is undefined.  ClassyC's extended guards (in the runtime safety trap) turn
 * this into a catchable RuntimeException (or compile-time static check when
 * possible) so it doesn't silently produce garbage or crash.
 *
 * Run:
 *   ./bin/classyc -g -fexceptions -I include bugs/009-shift-out-of-range.cy -eg
 */
#include <stdio.h>

int main() {
    printf("=== shift-range safety ===\n");

    int caught = 0;
    int val = 0x12345678;
    int big = 40;   /* >= 32 for int */
    int neg = -3;

    /* shift by >= width */
    try {
        int r = val << big;
        printf("FAIL: 0x%08x << 40 = 0x%08x\n", val, r);
    } catch (Exception e) {
        printf("PASS: caught wide shift: %s\n", e.msg);
        caught++;
    }

    /* negative shift amount */
    try {
        int r2 = val >> neg;
        printf("FAIL: >> -3 succeeded\n");
    } catch (Exception e) {
        printf("PASS: caught negative shift: %s\n", e.msg);
        caught++;
    }

    if (caught == 2) {
        printf("Shift OOB cases caught.\n");
        return 0;
    }
    return 1;
}
