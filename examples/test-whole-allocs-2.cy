/* test-whole-allocs-2.cy — second TU of the -fcheck-whole-allocs demo.
 * See examples/test-whole-allocs.cy for the run command.
 *
 * @expect: skip   (references symbols defined in test-whole-allocs.cy; only
 *                  meaningful when the two are compiled together) */
#include <stdio.h>
#include <stdlib.h>

void consume(char *p) {
    free(p);
}

void file2_returns(int n) {
    char *r = (char *)malloc(n);
    /* leaks unless -fauto-release */
    (void)r;
}

extern void file1_calls_helper(void);
extern void file1_leaks(void);

int main() {
    file1_calls_helper();
    file1_leaks();
    file2_returns(8);
    return 0;
}
