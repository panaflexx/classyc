/* Test 085: Dict with mixed data types */
#include <stdio.h>

int main() {
    dict d = {
        "string": "hello",
        "integer": 42,
        "float": 3.14,
        "boolean": 1,
        "null_value": null,
        "array": [1, 2, 3]
    };
    printf("mixed types: %s, %d, %f\n", d.string, d.integer, d.float);
    return 0;
}