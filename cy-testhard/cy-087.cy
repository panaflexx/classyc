/* Test 087: Lambda with nested function calls */
#include <stdio.h>

int square(int x) {
    return x * x;
}

int main() {
    int (*f)(int) = (int x) => square(x) + x;
    printf("nested calls: %d\n", f(5));
    return 0;
}