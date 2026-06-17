/* Test 048: Complex lambda with capture simulation */
#include <stdio.h>

int main() {
    int base = 10;
    int (*f)(int) = (int x) => x + base;
    printf("lambda capture simulation: %d\n", f(5));
    return 0;
}