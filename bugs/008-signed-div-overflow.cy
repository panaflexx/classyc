/* bugs/008-signed-div-overflow.cy
 *
 * C11 §6.5/5: Signed integer division INT_MIN / -1 (or % -1) overflows and
 * is undefined behavior (often raises SIGFPE on x86).  ClassyC's
 * gen_div_overflow_check converts this into a catchable RuntimeException
 * (reason 3, "division by zero" category) when -fexceptions is on.
 *
 * Run:
 *   ./bin/classyc -g -fexceptions -I include bugs/008-signed-div-overflow.cy -eg
 */
#include <stdio.h>

int main() {
    printf("=== signed division overflow safety ===\n");

    int caught = 0;
    int min32 = -2147483647 - 1;   /* INT_MIN */
    int neg1 = -1;

    /* INT_MIN / -1 */
    try {
        int r = min32 / neg1;
        printf("FAIL: INT_MIN / -1 = %d (no trap)\n", r);
    } catch (Exception e) {
        printf("PASS: caught INT_MIN/-1 overflow: %s\n", e.msg);
        caught++;
    }

    /* INT_MIN % -1 */
    try {
        int r2 = min32 % neg1;
        printf("FAIL: INT_MIN %% -1 = %d (no trap)\n", r2);
    } catch (Exception e) {
        printf("PASS: caught INT_MIN%%-1 overflow: %s\n", e.msg);
        caught++;
    }

    if (caught == 2) {
        printf("Both signed overflow cases produced catchable exceptions.\n");
        return 0;
    }
    printf("FAIL: only %d/2 cases caught\n", caught);
    return 1;
}
