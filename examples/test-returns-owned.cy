// test-returns-owned.cy — verifies caller-side acquisition via inferred
// summary.returns_owned_p.
//
//   bin/classyc -fno-exceptions -fownership-report examples/test-returns-owned.cy
//
// We expect:
//   - make_buf / make_strdup get returns_owned_p = TRUE
//   - leaks_via_make_buf: `b = make_buf(...)` becomes a tracked candidate
//     and the leak is reported (and auto-fixable with -fauto-release)
//   - cleans_via_make_buf: caller frees the buffer — no leak

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Allocates and returns - should infer returns_owned_p with release="free".
char *make_buf(int n) {
    char *b = (char *)malloc(n);
    return b;
}

// strdup wrapper - same idea.
char *make_dup(const char *s) {
    char *d = strdup(s);
    return d;
}

// Caller LEAKS the IP-discovered acquire.
void leaks_via_make_buf() {
    char *b = make_buf(32);
    b[0] = 'x';
}

// Caller correctly frees.
void cleans_via_make_buf() {
    char *c = make_buf(32);
    free(c);
}

// Caller leaks a strdup-wrapper result.
void leaks_via_make_dup() {
    char *d = make_dup("hello");
    printf("d=%s\n", d);
}

int main() {
    leaks_via_make_buf();
    cleans_via_make_buf();
    leaks_via_make_dup();
    return 0;
}
