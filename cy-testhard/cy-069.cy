/* Test 069: Lambda with complex nested logic */
#include <stdio.h>

int main() {
    int (*f)(int) = (int x) => {
        int result = 0;
        for (int i = 0; i < x; i++) {
            result += i;
        }
        return result;
    };
    printf("nested logic lambda: %d\n", f(5));
    return 0;
}