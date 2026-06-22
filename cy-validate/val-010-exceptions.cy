/* val-010-exceptions.cy — validates exceptions AND the always-on safety guards.
 *
 * KEY FACT (README is wrong, see SHORTCOMINGS.md A5): exceptions + JIT safety
 * guards are ON BY DEFAULT. This file is run with NO -fexceptions flag and the
 * guards (null deref, div-by-zero, array OOB) must still fire as catchable
 * exceptions.
 *
 * Also validates user-defined exceptions via plain enum constants (>= 100).
 *
 * Run (no special flags):  ./bin/classyc -g -I include cy-validate/val-010-exceptions.cy -eg
 */
#include <stdio.h>
#include <string.h>

/* user-defined exception ids (>= 100 is the user convention) */
enum { KeyNotFound = 100, ParseError = 101 };

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

void risky(int code) {
    if (code == 1) throw(NullException, "null");
    if (code == 2) throw(OutOfBoundsException, "oob");
    if (code == 3) throw(RuntimeException, "boom");
}

int lookup(int key) {
    if (key != 42) throw(KeyNotFound, "no such key");
    return 1;
}

int main() {
    printf("=== val-010 exceptions (default-on) ===\n\n");

    /* 1. basic throw/catch + field access */
    int got = 0;
    try { throw(NullException, "boom"); }
    catch (NullException e) { got = (strcmp(e.msg, "boom") == 0); }
    check(got == 1, "basic throw/catch reads e.msg");

    /* 2. typed multi-catch selects by id (cross-function throw) */
    int tags[3] = {0, 0, 0};
    for (int i = 1; i <= 3; i++) {
        try { risky(i); }
        catch (NullException e)        { tags[0] = 1; }
        catch (OutOfBoundsException e) { tags[1] = 1; }
        catch (Exception e)            { tags[2] = 1; }
    }
    check(tags[0] && tags[1] && tags[2], "typed multi-catch dispatches by id");

    /* 3. propagation through nested try */
    int outer = 0;
    try {
        try { throw(RuntimeException, "inner"); }
        catch (NullException e) { /* no match */ }
    } catch (Exception e) { outer = 1; }
    check(outer == 1, "unmatched inner propagates to outer");

    /* 4. user-defined enum exception (>= 100) */
    int uok = 0;
    try { lookup(7); }
    catch (KeyNotFound e) { uok = (strcmp(e.msg, "no such key") == 0); }
    check(uok == 1, "user-defined enum exception caught by name");

    /* 5. user exception does NOT match a different clause */
    int wrong = 0, right = 0;
    try { throw(ParseError, "bad"); }
    catch (KeyNotFound e) { wrong = 1; }
    catch (Exception e)   { right = 1; }
    check(!wrong && right, "distinct user exception ids don't cross-match");

    /* ---- SAFETY GUARDS (must be active with NO flag) ---- */

    /* 6. null pointer dereference -> NullException */
    int gn = 0;
    int *p = 0;
    try { int v = *p; if (v) gn = 0; }
    catch (NullException e) { gn = 1; }
    check(gn == 1, "GUARD: null deref throws NullException (default-on)");

    /* 7. integer division by zero -> arithmetic exception */
    int gd = 0, a = 42, b = 0;
    try { int r = a / b; if (r) gd = 0; }
    catch (Exception e) { gd = 1; }
    check(gd == 1, "GUARD: div-by-zero throws (default-on)");

    /* 8. array out-of-bounds -> OutOfBoundsException */
    int go = 0;
    int arr[4] = {1, 2, 3, 4};
    int idx = 9;
    try { int v = arr[idx]; if (v) go = 0; }
    catch (OutOfBoundsException e) { go = 1; }
    check(go == 1, "GUARD: array OOB throws OutOfBoundsException (default-on)");

    /* 9. body completes normally -> handler skipped */
    int ran_handler = 0, completed = 0;
    try { completed = 1; }
    catch (Exception e) { ran_handler = 1; }
    check(completed && !ran_handler, "normal completion skips handler");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
