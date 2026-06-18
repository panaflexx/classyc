/* Test 072: Lambda with ternary operator */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => x > 0 ? x * 2 : x * -1;
    printf("ternary lambda: %d, %d\n", f(5), f(-3));
    return 0;
}