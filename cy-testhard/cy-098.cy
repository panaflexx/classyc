/* Test 098: Complex lambda captures with defer */
#include <stdio.h>

int main() {
    int x = 10;
    int y = 20;

    auto f = (int a) => {
        int z = x + y + a;
        defer printf("defer in lambda: %d\n", z);
        return z * 2;
    };

    int result = f(5);
    printf("lambda result: %d\n", result);
    return 0;
}
