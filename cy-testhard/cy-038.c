/* Test 038: Lambda in array of function pointers */
#include <stdio.h>

int main() {
    int (*funcs[3])(int) = {
        (int x) => x + 1,
        (int x) => x * 2,
        (int x) => x * x
    };
    printf("array of lambdas: %d, %d, %d\n", funcs[0](5), funcs[1](5), funcs[2](5));
    return 0;
}