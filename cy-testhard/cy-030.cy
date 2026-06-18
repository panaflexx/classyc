/* Test 030: String with methods */
#include <stdio.h>

int main() {
    String s = "  Hello World  ";
    printf("trimmed: '%s'\n", s.trim());
    printf("upper: '%s'\n", s.upper());
    printf("length: %zu\n", s.length());
    return 0;
}