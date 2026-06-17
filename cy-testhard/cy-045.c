/* Test 045: Dict with dynamic key access */
#include <stdio.h>

int main() {
    dict d = { "name": "test" };
    char *key = "value";
    d[key] = 42;
    printf("dynamic key: %d\n", d.value);
    return 0;
}