/* Test 105: Generic List with complex operations */
#include <stdio.h>
#include "list.h"

int main() {
    List<int>* nums = new List<int>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    // Filter even
    auto evens = nums->Filter((int x) => x % 2 == 0);
    printf("evens: ");
    for (auto e in evens) printf("%d ", e);
    printf("\n");

    // Map to squares
    auto squares = nums->Map((int x) => x * x);
    printf("squares: ");
    for (auto s in squares) printf("%d ", s);
    printf("\n");

    // Reduce sum
    int sum = nums->Reduce((int acc, int x) => acc + x, 0);
    printf("sum: %d\n", sum);

    // Sort descending
    auto sorted = nums->Sort((int a, int b) => b - a);
    printf("sorted desc: ");
    for (auto s in sorted) printf("%d ", s);
    printf("\n");

    // Take/Skip
    auto first3 = nums->Take(3);
    auto skip3 = nums->Skip(3);
    printf("first3: "); for (auto f in first3) printf("%d ", f); printf("\n");
    printf("skip3: "); for (auto s in skip3) printf("%d ", s); printf("\n");

    // Chained operations
    auto result = nums
        ->Filter((int x) => x > 3)
        ->Map((int x) => x * 10)
        ->Take(3);
    printf("chained: ");
    for (auto r in result) printf("%d ", r);
    printf("\n");

    return 0;
}
