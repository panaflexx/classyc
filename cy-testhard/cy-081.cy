/* Test 081: Dict with dynamic assignment */
#include <stdio.h>

int main() {
    dict d = { "name": "test" };
    char *key = "dynamic";
    d[key] = "value";
    printf("dynamic assignment: %s\n", d.dynamic);
    return 0;
}