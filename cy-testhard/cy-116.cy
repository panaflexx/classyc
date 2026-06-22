/* Test 116: Lambda recursion and higher-order functions */
#include <stdio.h>

int main() {
    // Factorial via recursive lambda (using function pointer)
    int (*fact)(int) = NULL;
    fact = (int n) => n <= 1 ? 1 : n * fact(n - 1);
    printf("fact(5) = %d\n", fact(5));

    // Fibonacci
    int (*fib)(int) = NULL;
    fib = (int n) => n <= 1 ? n : fib(n - 1) + fib(n - 2);
    printf("fib(10) = %d\n", fib(10));

    // Higher-order: map with lambda
    int nums[] = {1, 2, 3, 4, 5};
    int (*square)(int) = (int x) => x * x;
    int (*cube)(int) = (int x) => x * x * x;

    auto apply = (int* arr, int len, int (*f)(int)) => {
        for (int i = 0; i < len; i++) arr[i] = f(arr[i]);
    };

    int nums2[] = {1, 2, 3, 4, 5};
    apply(nums2, 5, square);
    printf("squared: ");
    for (int i = 0; i < 5; i++) printf("%d ", nums2[i]);
    printf("\n");

    int nums3[] = {1, 2, 3, 4, 5};
    apply(nums3, 5, cube);
    printf("cubed: ");
    for (int i = 0; i < 5; i++) printf("%d ", nums3[i]);
    printf("\n");

    // Lambda returning lambda
    auto makeAdder = (int x) => (int y) => x + y;
    auto add5 = makeAdder(5);
    auto add10 = makeAdder(10);
    printf("add5(3) = %d, add10(3) = %d\n", add5(3), add10(3));

    return 0;
}
