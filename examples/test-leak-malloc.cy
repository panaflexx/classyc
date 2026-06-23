/* test-leak-malloc.cy — exercises the malloc/free leak detector.
 *
 * Run with:    bin/classyc -I include examples/test-leak-malloc.cy -eg
 *
 * Expected behavior (warnings, NOT errors):
 *   - `definite_leak`:    one warning on `a` (allocated, used, never freed)
 *   - `definite_leak2`:   one warning on `b` (allocated, never even read)
 *   - `released`:         no warning (free() consumes it)
 *   - `escaped_return`:   no warning (returned to caller)
 *   - `escaped_assign`:   no warning (stored into a global)
 *   - `escaped_call`:     no warning (passed to another function, conservative)
 *   - `unowned_silenced`: no warning (user opted out with `unowned`)
 *   - `calloc_leak`:      one warning on `c` (calloc-family)
 *   - `strdup_leak`:      one warning on `s` (strdup-family)
 *
 * Each non-leaking function still calls `free` itself or returns the pointer
 * so the program is actually leak-free at runtime; only the static checker
 * has anything to say.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *sink = (void *)0;

void take_ownership(void *p) {
    free(p);   /* helper that frees its argument */
}

/* ---- LEAK ---- bare malloc, used, never freed. */
void definite_leak() {
    char *a = (char *)malloc(16);
    a[0] = 'h'; a[1] = '\0';
    printf("leak.a written\n");
    /* no free, no return, no store */
}

/* ---- LEAK ---- bare malloc, never even touched. */
void definite_leak2() {
    int *b = (int *)malloc(sizeof(int));
    (void)b;   /* explicit no-op; no free either */
}

/* ---- OK ---- explicit free, no warning expected. */
void released() {
    char *p = (char *)malloc(8);
    p[0] = '\0';
    free(p);
}

/* ---- OK ---- value returned to caller, caller now owns. */
char *escaped_return() {
    char *q = (char *)malloc(8);
    q[0] = '\0';
    return q;
}

/* ---- OK ---- value stored into a global (sink). */
void escaped_assign() {
    char *r = (char *)malloc(8);
    sink = r;
}

/* ---- OK ---- value passed to a helper (conservative escape). */
void escaped_call() {
    char *s = (char *)malloc(8);
    take_ownership(s);
}

/* ---- OK ---- user-silenced via `unowned`. */
void unowned_silenced() {
    unowned char *u = (char *)malloc(8);
    u[0] = '\0';
    free(u);   /* user takes responsibility */
}

/* ---- LEAK ---- calloc family. */
void calloc_leak() {
    int *c = (int *)calloc(4, sizeof(int));
    c[0] = 1;
}

/* ---- LEAK ---- strdup family. */
void strdup_leak() {
    char *s = strdup("hello");
    s[0] = 'H';   /* mutate; no free, no return, no store */
}

/* ---- KNOWN FALSE NEGATIVE ---- strdup passed to printf.
 * v1's escape rule is conservative: any function call carrying the
 * binding (other than the recognised release) counts as a potential
 * ownership transfer.  Since the resource-pair table doesn't yet
 * include the printf family as "non-retaining consumer", this leaks at
 * runtime but the checker stays silent.  Documented limitation. */
void strdup_printf_no_warn() {
    char *t = strdup("world");
    printf("got %s\n", t);
    /* Real leak, but printf appears to consume `t`. */
}

int main() {
    definite_leak();
    definite_leak2();
    released();
    char *e = escaped_return();
    free(e);
    escaped_assign();
    free(sink);
    escaped_call();
    unowned_silenced();
    calloc_leak();
    strdup_leak();
    strdup_printf_no_warn();
    return 0;
}
