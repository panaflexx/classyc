/* Test 054: String with escape sequences */
#include <stdio.h>

int main() {
    String s = "Hello\nWorld\tTabbed";
    printf("escape sequences: %s\n", s);
    return 0;
}