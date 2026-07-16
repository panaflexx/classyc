/* val-048-shift-range.cy — shift amount range guards (C11 §6.5.7 / bugs/009).
 *
 * Under default-on exceptions, `<<` / `>>` with count < 0 or >= width throw
 * ArithmeticException ("shift out of range") via _safety_trap reason 5.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-048-shift-range.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main(void) {
    printf("=== val-048 shift-range safety ===\n\n");

    int val = 0x12345678;
    int big = 40;
    int neg = -3;
    int ok = 3;

    int caught = 0;
    try {
        int r = val << big;
        (void)r;
        check(0, "1a wide shift should throw");
    } catch (Exception e) {
        check(e.msg != NULL && strstr(e.msg, "shift") != NULL,
              "1a wide shift throws (msg mentions shift)");
        caught++;
    }

    try {
        int r = val >> neg;
        (void)r;
        check(0, "1b negative shift should throw");
    } catch (Exception e) {
        check(1, "1b negative shift throws");
        caught++;
    }

    try {
        int r = val << ok;
        check(r != 0 || ok == 0, "1c in-range shift succeeds");
        caught++;
    } catch (Exception e) {
        check(0, "1c in-range should not throw");
    }

    /* <<= assign form */
    try {
        int x = 1;
        x <<= 40;
        check(0, "2a <<= wide should throw");
    } catch (Exception e) {
        check(1, "2a <<= wide throws");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
