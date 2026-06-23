/* val-019-loop-arena.cy -- per-iteration arena reclamation (no manual
 * c2m_str_checkpoint / release_to) for tight loops whose allocations only
 * happen INSIDE helper calls.
 *
 * Reproduces the classy-fetch.cy pattern in a network-free, deterministic
 * form.  Three properties are checked:
 *
 *   (a) CROSS-FUNCTION DETECTION  --  subtree_allocates_string_p must see
 *       that calls returning `String` allocate in the caller's scope, so
 *       a function that allocates Strings only via helpers (no inline `+`
 *       or method call on a literal) still activates the String arena.
 *
 *   (b) PER-ITERATION RECLAMATION  --  the compiler must take a fresh
 *       checkpoint at the top of every loop body and release at the
 *       back-edge.  Without this, a 200k-iteration loop allocates 200k
 *       Strings that all live until the enclosing function returns and
 *       RSS grows linearly with the loop count.
 *
 *   (c) RETURN PROTECTION  --  per-iter release must NOT free Strings that
 *       were promoted out of the iteration via `return` (they are protected
 *       by release_keeping at the FUNCTION-level mark only).
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-019-loop-arena.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Cross-function String allocator: returns a freshly allocated tracked
   String, just like List<String>.join(), json(), Http.get(...).asDict()->
   leaves, etc.  The CALLER must auto-checkpoint for memory to stay bounded. */
String label(int i) {
    return (String)"x#" + i;
}

/* Reads current resident set size (kB) from /proc/self/status -- portable
   enough for Linux JIT runs.  Returns -1 if unavailable. */
long rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, " %ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}

/* Returns a fresh String from within a per-iter scope -- proves the
   function-level release_keeping protects the return across the per-iter
   release that fires at the bottom of the (caller's) loop body. */
String make_in_loop(int rounds) {
    String last = (String)"start";
    for (int i = 0; i < rounds; i++) {
        last = label(i);   /* every iter allocates via helper */
    }
    return last;           /* must survive across per-iter releases */
}

int main(void) {
    printf("=== val-019 per-iteration arena reclamation ===\n\n");

    /* (a) CROSS-FUNCTION DETECTION.
       This single direct call returns a tracked String.  After this
       statement, the next 200k iterations must not blow memory. */
    String head = label(0);
    check(strcmp(head, "x#0") == 0, "(a) helper-returned String reaches caller");

    /* (b) PER-ITERATION RECLAMATION.
       200k iterations where the ONLY allocation site is inside `label(i)`.
       The OLD compiler (without the cross-function detector) never activated
       a String scope here (no inline N_CONCAT in main) and would grow RSS
       unboundedly; the NEW compiler activates and per-iter releases. */
    long before = rss_kb();
    for (int i = 0; i < 200000; i++) {
        String t = label(i);
        if (i == 199999 && strcmp(t, "x#199999") != 0) failed++;
    }
    long after = rss_kb();
    long grew_kb = (before > 0 && after > 0) ? (after - before) : -1;

    if (grew_kb < 0) {
        /* /proc/self/status unreadable; weaker check: at least we did not crash */
        check(1, "(b) 200k-iter loop completed (RSS not measurable)");
    } else {
        /* With per-iter release, growth should be well under 5 MB even
           accounting for arena slack, JIT allocation, dict structures, etc.
           Without it, 200k * ~32B (string body) + tracker entries would push
           growth past 10-20 MB.  Use a generous bound (8 MB) to stay robust
           against allocator pre-allocation and noise. */
        printf("    [RSS before=%ldkB after=%ldkB grew=%ldkB]\n", before, after, grew_kb);
        check(grew_kb < 8 * 1024,
              "(b) 200k-iter helper-allocations stay bounded (<8MB RSS growth)");
    }

    /* (c) RETURN PROTECTION through nested scopes.
       make_in_loop() runs its OWN per-iter scope around `last = label(i);`,
       which releases every iteration's intermediate String -- yet the FINAL
       `last` must survive across that release_to(per_iter_mark) and back to
       us.  This works only if (i) the per-iter mark is correctly < the
       function-level mark so the function-level release_keeping fires on
       N_RETURN, and (ii) the loop's bottom release runs BEFORE the function
       reaches its N_RETURN keep path. */
    String tail = make_in_loop(5000);
    check(strcmp(tail, "x#4999") == 0,
          "(c) String returned from inside a per-iter loop scope survives");

    /* (b2) BREAK out of an allocating loop -- the iteration that calls
       `break` must release its per-iter allocations (otherwise a tight
       early-exit loop would leak one iteration's worth per call). */
    long b_before = rss_kb();
    for (int j = 0; j < 1000; j++) {
        for (int i = 0; i < 1000; i++) {
            String s = label(i * j);
            if (i == 500) break;   /* exits inner; must release iter 500 */
        }
    }
    long b_after = rss_kb();
    long b_grew = (b_before > 0 && b_after > 0) ? (b_after - b_before) : -1;
    if (b_grew < 0) {
        check(1, "(b2) nested break loop completed (RSS not measurable)");
    } else {
        printf("    [RSS before=%ldkB after=%ldkB grew=%ldkB]\n",
               b_before, b_after, b_grew);
        check(b_grew < 8 * 1024,
              "(b2) 1000x1000 helper-allocations with break stay bounded (<8MB)");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
