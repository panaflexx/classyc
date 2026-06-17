/* Test 092: Dict with nested arrays */
#include <stdio.h>

int main() {
    dict d = {
        "items": [
            { "name": "item1", "value": 1 },
            { "name": "item2", "value": 2 }
        ]
    };
    printf("nested arrays: %s = %d\n", d.items[0].name, d.items[0].value);
    return 0;
}