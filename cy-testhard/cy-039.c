/* Test 039: Nested lambda expressions */
#include <stdio.h>

int main() {
    int (*outer)(int) = (int x) => {
        int (*inner)(int) = (int y) => y * 2;
        return inner(x);
    };
    printf("nested lambda: %d\n", outer(5));
    return 0;
}