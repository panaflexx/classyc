/* bugs/005-negative-index.cy
 *
 * Verifies that negative indices on String::substr and the positional
 * form of String::replace now produce a catchable RuntimeException
 * when compiled with -fexceptions.
 *
 * Compile & run:
 *   ./bin/classyc -g -fexceptions -I include bugs/005-negative-index.cy -eg
 */

#include <stdio.h>

int main() {
    printf("=== negative-index safety test ===\n");

    String s = "hello";
    int caught = 0;

    /* --- substr negative pos --- */
    try {
        String bad = s.substr(-1, 2);
        printf("FAIL: substr(-1,2) should have thrown\n");
    } catch (e) {
        printf("PASS: caught substr OOB: %s (line %d)\n", e.msg, e.line);
        caught++;
    }

    /* --- replace negative pos --- */
    try {
        String r = s.replace(-3, 2, "xx");
        printf("FAIL: replace(-3,2,...) should have thrown\n");
    } catch (e) {
        printf("PASS: caught replace OOB: %s (line %d)\n", e.msg, e.line);
        caught++;
    }

    /* --- replace negative len --- */
    try {
        String r2 = s.replace(1, -1, "yy");
        printf("FAIL: replace(1,-1,...) should have thrown\n");
    } catch (e) {
        printf("PASS: caught replace OOB (len): %s (line %d)\n", e.msg, e.line);
        caught++;
    }

    if (caught == 3) {
        printf("All negative-index cases produced catchable exceptions.\n");
        return 0;
    } else {
        printf("FAIL: only %d/3 cases caught\n", caught);
        return 1;
    }
}
