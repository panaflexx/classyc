/* Test 009: Dict in for-in loop */
#include <stdio.h>

int main() {
    dict d = { "a": 1, "b": 2, "c": 3 };
    printf("dict for-in: ");
    for (auto k in d) {
        printf("%s ", k);
    }
    printf("\n");
    return 0;
}