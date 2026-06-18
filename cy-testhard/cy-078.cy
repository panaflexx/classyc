/* Test 078: Dict with for-in key-value pairs */
#include <stdio.h>

int main() {
    dict d = { "a": 1, "b": 2 };
    printf("key-value for-in: ");
    for (auto k, v in d) {
        printf("%s=%d ", k, v);
    }
    printf("\n");
    return 0;
}