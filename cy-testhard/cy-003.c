/* Test 003: Lambda in higher-order function */
#include <stdio.h>

int apply(int x, int (*f)(int)) {
    return f(x);
}

int main() {
    int result = apply(7, (int x) => x + 10);
    printf("higher-order: %d\n", result);
    return 0;
}