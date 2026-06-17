/* Test 005: Lambda in array */
#include <stdio.h>

int main() {
    int (*funcs[2])(int) = {
        (int x) => x * 2,
        (int x) => x * 3
    };
    printf("lambda array: %d, %d\n", funcs[0](5), funcs[1](5));
    return 0;
}