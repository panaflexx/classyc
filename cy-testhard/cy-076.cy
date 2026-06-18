/* Test 076: Lambda with function pointer in array */
#include <stdio.h>

int square(int x) {
    return x * x;
}

int main() {
    int (*funcs[2])(int) = {square, (int x) => x * 2};
    printf("function pointer array: %d, %d\n", funcs[0](5), funcs[1](5));
    return 0;
}