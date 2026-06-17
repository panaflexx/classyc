/* Test 034: Complex lambda with closure-like behavior */
#include <stdio.h>

int main() {
    int multiplier = 3;
    int (*f)(int) = (int x) => x * multiplier;
    printf("lambda closure: %d\n", f(7));
    return 0;
}