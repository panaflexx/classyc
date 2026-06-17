/* Test 043: Lambda with complex return type */
#include <stdio.h>

int main() {
    auto f = (int x) => {
        if (x > 0) {
            return x * 2;
        } else {
            return x * -1;
        }
    };
    printf("lambda complex return: %d, %d\n", f(5), f(-3));
    return 0;
}