/* Test 058: Dict with JSON parsing */
#include <stdio.h>

int main() {
    dict d = json("{\"name\":\"test\",\"value\":42}");
    printf("json parse: %s = %d\n", d.name, d.value);
    return 0;
}