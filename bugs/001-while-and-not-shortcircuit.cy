/* bugs/001 — minimal reproducer for the MIR-JIT (c2mir) miscompilation of a
 * `while (A && !(B && C))` short-circuit condition.
 *
 *   Expected output (gcc, clang):  got=2  -> PASS
 *   Actual   output (classyc -eg): got=5  -> FAIL (miscompiled)
 *
 * Build / run:
 *   gcc:     cp bugs/001-while-and-not-shortcircuit.cy /tmp/b.c && cc /tmp/b.c -o /tmp/b && /tmp/b
 *   classyc: bin/classyc bugs/001-while-and-not-shortcircuit.cy -eg
 */
#include <stdio.h>

/* Scan a buffer for the first CRLF and return its index.
 * The bug is in the loop CONDITION form below. */
static long scan_cond(char *s, long n) {
    long j = 0;
    /* MISCOMPILED by c2mir: the `&& !( … && … )` is evaluated wrongly, so the
     * loop never stops at the CRLF and runs to the end of the buffer. */
    while (j + 1 < n && !(s[j] == '\r' && s[j + 1] == '\n')) j++;
    return j;
}

/* Logically identical, written with an explicit `break`.  Compiles correctly. */
static long scan_break(char *s, long n) {
    long j = 0;
    while (j + 1 < n) {
        if (s[j] == '\r' && s[j + 1] == '\n') break;
        j++;
    }
    return j;
}

int main(void) {
    char buf[] = "AB\r\nCD";   /* CRLF begins at index 2 */
    long a = scan_cond(buf, 6);
    long b = scan_break(buf, 6);
    printf("scan_cond  got=%ld expected=2 -> %s\n", a, a == 2 ? "PASS" : "FAIL (miscompiled)");
    printf("scan_break got=%ld expected=2 -> %s\n", b, b == 2 ? "PASS" : "FAIL");
    return (a == 2) ? 0 : 1;
}
