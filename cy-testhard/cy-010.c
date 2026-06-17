/* Test 010: Dict with in operator */
#include <stdio.h>

int main() {
    dict d = { "name": "test", "value": 42 };
    if ("name" in d) {
        printf("name key exists\n");
    }
    if ("missing" not in d) {
        printf("missing key does not exist\n");
    }
    return 0;
}