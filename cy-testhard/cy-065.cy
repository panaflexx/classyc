/* Test 065: Lambda with return statement in block */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => {
        if (x > 0) {
            return x * 2;
        } else {
            return x * -1;
        }
    };
    printf("lambda with return: %d, %d\n", f(5), f(-3));
    return 0;
}