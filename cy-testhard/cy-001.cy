/* Test 001: Lambda block expression */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => {
        return x * x;
    };
    printf("lambda square: %d\n", f(4));
    return 0;
}