/* Test 073: String with various encoding */
#include <stdio.h>

int main() {
    String utf8 = "Schöne Grüße 😊";
    String ascii = "Hello World";
    printf("UTF-8: %s\n", utf8);
    printf("ASCII: %s\n", ascii);
    return 0;
}