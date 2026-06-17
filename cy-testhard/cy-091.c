/* Test 091: Lambda with complex expressions in block */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => {
        int a = x * 2;
        int b = a + 1;
        return b * 3;
    };
    printf("complex block: %d\n", f(5));
    return 0;
}