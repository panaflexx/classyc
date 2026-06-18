/* Test 056: Lambda with multiple parameters and complex logic */
#include <stdio.h>

int main() {
    int (*f)(int, int, int) = (int a, int b, int c) => {
        int sum = a + b;
        return sum * c;
    };
    printf("multi-param lambda: %d\n", f(2, 3, 4));
    return 0;
}