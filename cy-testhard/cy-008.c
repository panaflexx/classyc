/* Test 008: Dict with bracket access */
#include <stdio.h>

int main() {
    dict d = { "name": "test", "value": 42 };
    d["newkey"] = "newvalue";
    printf("bracket access: %s = %s\n", "newkey", d["newkey"]);
    return 0;
}