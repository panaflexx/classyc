// test-interproc.cy - exercises the interprocedural ownership pass.
//
// Without IP analysis, `helper_release()` looks opaque to callers and
// `client_via_helper()` gets reported as a leak (escape via call).  With
// IP inference, `helper_release` is detected as PA_RELEASES and the
// caller's malloc is recognized as freed.  Verify with -fownership-report.
//
//   bin/classyc -fownership-report -fno-exceptions examples/test-interproc.cy

#include <stdio.h>
#include <stdlib.h>

// Wrapper that takes ownership and frees - should infer ((releases)).
void helper_release(char *p) {
    free(p);
}

// Wrapper that just reads - should infer ((borrows)).
void helper_read(char *p) {
    if (p) printf("len=%zu\n", (size_t)1);  // bogus read; just touches p
}

// Wrapper that allocates and returns - should be returns_owned_p.
char *helper_alloc(int n) {
    char *m = (char *)malloc(n);
    return m;
}

// Client: would leak pre-IP because `helper_release(p)` looked opaque.
// With IP analysis the leak warning should disappear.
void client_via_helper() {
    char *p = (char *)malloc(16);
    helper_release(p);
}

// Client: leak - the read-only wrapper doesn't take ownership.
// IP analysis should KEEP this as a leak (borrowing helper doesn't free).
void client_borrows_only() {
    char *q = (char *)malloc(16);
    helper_read(q);
}

int main() {
    client_via_helper();
    client_borrows_only();
    return 0;
}
