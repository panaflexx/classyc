/* Test 061: Lambda in method parameter */
#include <stdio.h>

int apply(int x, int (*f)(int)) {
    return f(x);
}

int main() {
    int result = apply(5, (int x) => x * 3);
    printf("lambda parameter: %d\n", result);
    return 0;
}