/* Test 006: Basic dict creation */
#include <stdio.h>

int main() {
    dict d = { "name": "test", "value": 42 };
    printf("dict: %s = %d\n", d.name, d.value);
    return 0;
}