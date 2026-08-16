#include <stdio.h>
int main() {
    int i = 2;
    char *bad = "hello" + i;            // pointer arithmetic: skips 2 chars -> "llo"
    printf("bad:   %s\n", bad);

    String good1 = (String)"hello" + i; // forces concatenation -> "hello2"
    printf("good1: %s\n", (char*)good1);

    String base = "hello";
    String good2 = base + i;            // base is already String -> "hello2"
    printf("good2: %s\n", (char*)good2);
    return 0;
}
