/* Test 062: Dict with array access */
#include <stdio.h>

int main() {
    dict d = { "items": [1, 2, 3] };
    printf("array access: %d\n", d.items[1]);
    return 0;
}