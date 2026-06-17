/* Test 074: Dict with different key types */
#include <stdio.h>

int main() {
    dict d = {
        "string_key": "value1",
        42: "value2", 
        "bool_key": 1
    };
    printf("mixed keys: %s, %s\n", d.string_key, d[42]);
    return 0;
}