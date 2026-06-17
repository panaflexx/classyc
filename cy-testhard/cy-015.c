/* Test 015: String in dict */
#include <stdio.h>

int main() {
    dict d = { "message": "Hello", "count": 42 };
    printf("dict string: %s = %d\n", d.message, d.count);
    return 0;
}