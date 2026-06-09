#include <stdio.h>

int main() {
    // Basic type deduction (available since C11)
    auto i = 42;              // Deduced as int
    auto d = 3.14;            // Deduced as double
    auto s = "Hello, World!"; // Deduced as const char*

    printf("i: %d (type: int)\n", i);
    printf("d: %.2f (type: double)\n", d);
    printf("s: %s (type: const char*)\n", s);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
    // C23-specific: deduction from braced-initializer lists
    auto arr = {1, 2, 3};     // Deduced as int[3]
    auto init = {4};          // Deduced as int[1]

    printf("arr[0]: %d (type: int[3])\n", arr[0]);
    printf("init[0]: %d (type: int[1])\n", init[0]);
#endif // C23

    return 0;
}
