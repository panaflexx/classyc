/* Test 059: String with substring operations */
#include <stdio.h>

int main() {
    String s = "Hello World";
    printf("substring: %s\n", s.substr(6, 5));
    printf("slice: %s\n", s.slice(0, 5));
    return 0;
}