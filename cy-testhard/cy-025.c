/* Test 025: Auto with dict */
#include <stdio.h>

int main() {
    auto d = { "name": "test", "value": 42 };
    printf("auto dict: %s = %d\n", d.name, d.value);
    return 0;
}