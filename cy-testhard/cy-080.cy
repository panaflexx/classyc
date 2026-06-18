/* Test 080: Lambda with complex conditionals */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => {
        if (x > 10) return x * 2;
        else if (x > 5) return x * 3;
        else return x;
    };
    printf("complex conditionals: %d, %d, %d\n", f(15), f(7), f(3));
    return 0;
}