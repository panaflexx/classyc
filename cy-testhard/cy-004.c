/* Test 004: Lambda with multiple parameters */
#include <stdio.h>

int main() {
    int (*f)(int, int) = (int a, int b) => a + b;
    printf("multi-param lambda: %d\n", f(3, 4));
    return 0;
}