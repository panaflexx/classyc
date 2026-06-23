/* test-leak-paths.cy — exercises the flow-sensitive analysis.
 *
 * Each function below has a comment annotating the expected diagnostics.
 * Run with: bin/classyc -I include examples/test-leak-paths.cy -eg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *sink;

/* ---- PATH-SENSITIVE LEAK ---- only the `else` branch leaks. */
void leak_on_one_branch(int cond) {
    char *p = (char *)malloc(16);
    if (cond) {
        free(p);
    }
    /* else branch falls through with `p` still Owned -> potential leak */
}

/* ---- OK ---- both branches release. */
void released_on_both_branches(int cond) {
    char *p = (char *)malloc(16);
    if (cond) {
        free(p);
    } else {
        free(p);
    }
}

/* ---- OK ---- early return frees on one branch, returns ownership on the other. */
char *conditional_transfer(int cond) {
    char *p = (char *)malloc(16);
    if (!cond) {
        free(p);
        return (void *)0;
    }
    return p;          /* `p` escapes to caller */
}

/* ---- ERROR ---- use after free. */
void use_after_free() {
    char *p = (char *)malloc(16);
    free(p);
    p[0] = 'x';        /* UAF — `p` was already freed */
}

/* ---- ERROR ---- double free. */
void double_free() {
    char *p = (char *)malloc(16);
    free(p);
    free(p);           /* double-free */
}

/* ---- OK ---- mixed control flow, both arms cleanup. */
void mixed_cleanup(int x) {
    char *p = (char *)malloc(16);
    if (x > 0) {
        p[0] = 'h';
        free(p);
    } else if (x < 0) {
        free(p);
    } else {
        sink = p;       /* escape via store */
    }
}

int main() {
    leak_on_one_branch(0);
    leak_on_one_branch(1);
    released_on_both_branches(1);
    char *e = conditional_transfer(1);
    if (e) free(e);
    /* use_after_free();   // intentionally not called — UAF at runtime if we did */
    /* double_free();      // intentionally not called */
    mixed_cleanup(-1);
    return 0;
}
