/* test-ownership-report.cy — exercises the structured `-fownership-report` dump.
 *
 *   bin/classyc -I include -fownership-report -fauto-release examples/test-ownership-report.cy
 *
 * The report should group `Buffer::*` methods under one `class Buffer` header,
 * and show the disposition we observed for each tracked binding: returned,
 * stored, freed, auto-released, leaked.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class Buffer {
    char *data;

    /* explicit free: should report "freed by release fn" */
    void load() {
        char *tmp = (char *)malloc(64);
        tmp[0] = '\0';
        free(tmp);
    }

    /* escape via assign to a class field: "stored into non-tracked location" */
    void grow(int n) {
        char *fresh = (char *)malloc(n);
        this.data = fresh;
    }

    /* leak that -fauto-release fixes silently */
    void scratch() {
        char *junk = (char *)malloc(8);
        junk[0] = 'x';
    }
};

/* top-level function: returned to caller */
char *make_name(int i) {
    char *s = (char *)malloc(16);
    snprintf(s, 16, "id-%d", i);
    return s;
}

/* top-level function: definite leak (auto-released under -fauto-release) */
void definitely_leaks() {
    int *p = (int *)calloc(4, sizeof(int));
    p[0] = 7;
}

int main() {
    char *n = make_name(42);
    free(n);
    definitely_leaks();
    return 0;
}
