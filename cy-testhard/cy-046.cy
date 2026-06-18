/* Test 046: String with auto-cast concatenation */
#include <stdio.h>

int main() {
    int num = 42;
    bool flag = 1;
    String s = "Number: " + num + ", Flag: " + flag;
    printf("auto-cast: %s\n", s);
    return 0;
}