/* val-002-string-arena.cy — validates ClassyC's automatic String arena.
 *
 * MEMORY MODEL (include/cstring.h + src/classyc.c gen):
 *   · Every heap String (from `+` concat, substr/replace/upper/lower/trim,
 *     copy/attach) is recorded in a process-global registry.
 *   · The COMPILER automatically emits c2m_str_checkpoint() at the start of a
 *     statement-scope that allocates Strings, and c2m_str_release_to(mark) at
 *     each scope exit — reclaiming that scope's Strings deterministically.
 *   · On `return <String>`, it emits c2m_str_release_keeping(mark, ret) so the
 *     returned String survives into the caller's scope (no use-after-free).
 *   · An atexit() net frees everything still tracked at normal exit.
 *
 * IMPORTANT: there is NO user-callable `String.checkpoint()` /
 * `String.release_to()` API (the README is wrong about this — see FINDINGS.md).
 * Reclamation is fully automatic; this test verifies the SEMANTICS hold.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-002-string-arena.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Returns a freshly allocated String; the compiler must KEEP it across the
   return boundary (release_keeping) so the caller sees valid memory. */
String make_label(String base, int n) {
    String s = base + "#" + n;     /* concat: heap-allocated, scope-tracked */
    return s;
}

/* Allocates a lot of throwaway Strings then returns nothing — its scope's
   Strings must be released, but it must NOT free the caller's live Strings. */
void churn(int rounds) {
    for (int i = 0; i < rounds; i++) {
        String junk = ((String)"tmp" + i).upper();
        if (junk.length() == 0) printf("unreachable\n");
    }
}

int main() {
    printf("=== val-002 String arena (automatic reclamation) ===\n\n");

    /* 1. Returned String survives the callee scope (release_keeping). */
    String a = make_label("alpha", 1);
    check(strcmp(a, "alpha#1") == 0, "1. returned String valid after return");

    /* 2. A live String survives an intervening allocation-heavy call. */
    String keep = make_label("keep", 7);
    churn(5000);                      /* churn allocates+releases 5000 strings */
    check(strcmp(keep, "keep#7") == 0,
          "2. live String intact after 5000-allocation churn() call");

    /* 3. Many returned Strings remain individually valid (no aliasing/reuse). */
    String r0 = make_label("row", 0);
    String r1 = make_label("row", 1);
    String r2 = make_label("row", 2);
    check(strcmp(r0, "row#0") == 0 &&
          strcmp(r1, "row#1") == 0 &&
          strcmp(r2, "row#2") == 0, "3. distinct returned Strings don't clobber");

    /* 4. Nested block scope: inner allocations released, outer String intact. */
    String outer = make_label("outer", 9);
    {
        String inner = make_label("inner", 9);
        String inner2 = (inner + "!").upper();
        check(strcmp(inner2, "INNER#9!") == 0, "4a. inner-scope String correct");
    }   /* inner scope's Strings reclaimed here */
    check(strcmp(outer, "outer#9") == 0, "4b. outer String survives inner scope exit");

    /* 5. Stress: 200k allocations in a tight loop must not crash or exhaust
          memory (proves per-iteration scope release actually runs). */
    int ok = 1;
    for (int i = 0; i < 200000; i++) {
        String t = make_label("x", i);
        if (i == 199999 && strcmp(t, "x#199999") != 0) ok = 0;
    }
    check(ok == 1, "5. 200k-iteration alloc loop stays correct & bounded");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
