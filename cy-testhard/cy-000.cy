/* Test 000: Basic lambda expression */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => x * 2;
    printf("lambda result: %d\n", f(5));
    return 0;
}
