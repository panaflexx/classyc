/* Test 088: String with various methods */
#include <stdio.h>

int main() {
    String s = "  Hello World  ";
    printf("methods: '%s', '%s', %zu\n", s.trim(), s.upper(), s.length());
    return 0;
}