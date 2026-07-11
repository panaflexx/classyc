/* Test 011: Dict json serialization */
#include <stdio.h>

int main() {
    dict d = { "name": "test", "value": 42 };
    printf("json: %s\n", d.json());
    return 0;
}