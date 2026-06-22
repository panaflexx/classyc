/* Test 095: Nested try/catch with re-throw */
#include <stdio.h>

int main() {
    int caught = 0;
    try {
        try {
            throw(RuntimeException, "inner");
        } catch (RuntimeException e) {
            printf("caught inner: %s\n", e.msg);
            caught = 1;
            throw(RuntimeException, "rethrow");
        }
    } catch (RuntimeException e) {
        printf("caught outer: %s\n", e.msg);
        caught = 2;
    }
    printf("nested rethrow: %d\n", caught);
    return 0;
}
