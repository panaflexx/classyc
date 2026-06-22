/* Test 124: Complex constexpr and compile-time evaluation */
#include <stdio.h>

constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

constexpr int fibonacci(int n) {
    return n <= 1 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

constexpr int sumArray(int* arr, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) sum += arr[i];
    return sum;
}

constexpr int arr[] = {1, 2, 3, 4, 5};

int main() {
    // Compile-time constants
    constexpr int fact5 = factorial(5);
    constexpr int fib10 = fibonacci(10);
    constexpr int arrSum = sumArray(arr, 5);

    printf("factorial(5) = %d\n", fact5);
    printf("fibonacci(10) = %d\n", fib10);
    printf("array sum = %d\n", arrSum);

    // Use in array sizes
    int buffer[fact5];  // 120 elements
    printf("buffer size: %zu\n", sizeof(buffer) / sizeof(int));

    // Constexpr String (if supported)
    constexpr String const_str = "compile-time";
    printf("const string: %s\n", const_str);

    return 0;
}
