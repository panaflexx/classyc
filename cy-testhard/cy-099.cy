/* Test 099: String methods chaining with edge cases */
#include <stdio.h>

int main() {
    String s = "  Hello World  ";

    // Chain multiple methods
    String r = s.trim().upper().replace("WORLD", "CLASSYC").lower();
    printf("chained: '%s'\n", r);

    // Empty string edge cases
    String empty = "";
    printf("empty length: %zu\n", empty.length());
    printf("empty empty: %d\n", empty.empty());
    printf("empty find: %zu\n", empty.find("x"));
    printf("empty substr: '%s'\n", empty.substr(0, 5));

    // Single char
    String one = "a";
    printf("one char upper: '%s'\n", one.upper());
    printf("one char lower: '%s'\n", one.lower());

    return 0;
}
