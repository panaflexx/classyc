/* Test 052: Lambda with complex expression */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => (x * x) + (x * 2) + 1;
    printf("complex lambda: %d\n", f(3));
    return 0;
}