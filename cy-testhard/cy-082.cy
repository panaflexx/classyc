/* Test 082: String with multiple concatenations */
#include <stdio.h>

int main() {
    String s1 = "Hello";
    String s2 = " ";
    String s3 = "World";
    String s4 = "!";
    String result = s1 + s2 + s3 + s4;
    printf("multiple concat: %s\n", result);
    return 0;
}