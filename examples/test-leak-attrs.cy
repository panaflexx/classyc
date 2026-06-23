/* test-leak-attrs.cy — exercises Step H: parameter attributes.
 *
 * KNOWN LIMITATION: classyc currently does not pre-define `__GNUC__`, so
 * including <stdio.h>, <stdlib.h>, etc. causes `<sys/cdefs.h>` (or the
 * macOS equivalent) to `#define __attribute__(x)` to nothing — which would
 * erase the very annotations this test is meant to demonstrate.  To keep
 * the test self-contained we forward-declare the few stdlib bits we need
 * with classyc's own extern syntax and avoid the standard headers.
 *
 * Once the parser learns to accept trailing attribute-specs on function
 * declarations, the `__GNUC__` predefine can be restored and this test can
 * use the normal `#include <stdio.h>` shape.
 */

/* Bare-bones forward declarations — no system headers, so cdefs.h never
 * runs and __attribute__((...)) survives preprocessing. */
extern void *malloc(unsigned long);
extern void  free(void *);
extern int   printf(const char *, ...);

/* A user-defined consumer that does NOT retain its pointer arg.  Without
 * the attribute the analyzer conservatively escapes `p` at the call site,
 * which would silence the leak check. */
void log_msg(const char *msg __attribute__((borrows))) {
    printf("log: %s\n", msg);
}

/* A user-defined destroyer that takes ownership and frees its arg.
 * `((releases))` on the parameter tells the analyzer the call site has
 * the same effect as `free(p)` — caller's binding becomes Released. */
void my_destroyer(void *p __attribute__((releases))) {
    free(p);
}

/* User-defined automatic cleanup via GCC's standard `((cleanup(fn)))`.
 * The compiler runs `auto_free_p(&local)` at scope exit so the local is
 * disposed of without manual `free`.  The analyzer skips its leak check
 * for these bindings. */
static void auto_free_p(char **pp) {
    if (*pp) free(*pp);
}

/* ---- LEAK ---- log_msg has ((borrows)), so `p` is NOT escaped by the
 * call.  Without freeing `p` ourselves, this is a real leak. */
void borrows_keeps_owned_leaks() {
    char *p = (char *)malloc(8);
    p[0] = '\0';
    log_msg(p);              /* state stays Owned thanks to ((borrows)) */
    /* no free — analyzer should fire leak warning */
}

/* ---- OK ---- log_msg has ((borrows)) AND we free p ourselves. */
void borrows_then_freed() {
    char *p = (char *)malloc(8);
    p[0] = '\0';
    log_msg(p);              /* Owned */
    free(p);                  /* Released — clean */
}

/* ---- OK ---- my_destroyer has ((releases)), so `p` is Released by the call. */
void releases_via_helper() {
    char *p = (char *)malloc(8);
    p[0] = '\0';
    my_destroyer(p);         /* state -> Released; no further free needed */
}

/* ---- ERROR ---- double-free via ((releases)) helper then explicit free. */
void releases_then_free() {
    char *p = (char *)malloc(8);
    p[0] = '\0';
    my_destroyer(p);         /* state -> Released */
    free(p);                  /* should fire double-free */
}

/* ---- OK ---- `((cleanup(auto_free_p)))` suppresses the leak diagnostic. */
void cleanup_attr() {
    char *p __attribute__((cleanup(auto_free_p))) = (char *)malloc(8);
    p[0] = 'h';
    /* no manual free; auto_free_p runs at scope exit, analyzer stays quiet */
}

int main() {
    borrows_keeps_owned_leaks();
    borrows_then_freed();
    releases_via_helper();
    /* releases_then_free();  // intentional double-free if called */
    cleanup_attr();
    return 0;
}
