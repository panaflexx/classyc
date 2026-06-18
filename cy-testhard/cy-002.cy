/* Test 002: Zero-arg lambda */
#include <stdio.h>

int main() {
    int (*f)() = () => 42;
    printf("zero-arg lambda: %d\n", f());
    return 0;
}