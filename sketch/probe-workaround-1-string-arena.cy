#include <stdio.h>
String build(int i) {
    String s = (String)"item-" + i;   // tracked automatically, no manual checkpoint needed
    return s;                          // survives the automatic release for the caller
}
int main() {
    for (int i = 0; i < 3; i++) {
        String s = build(i);
        printf("%s\n", (char*)s);
    }
    return 0;
}
