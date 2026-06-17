/* Test 036: String with various operations */
#include <stdio.h>

int main() {
    String s = "Hello World";
    printf("length: %zu\n", s.length());
    printf("substring: %s\n", s.substr(0, 5));
    printf("find: %d\n", s.find("World"));
    return 0;
}