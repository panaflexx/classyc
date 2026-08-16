#include <stdio.h>
int main() {
    try {
        for (int i = 0; i < 5; i++) {
            String s = (String)"leaked-" + i;
            printf("iter %d: %s\n", i, (char*)s);
        }
        throw(RuntimeException, "oops");
    } catch (Exception e) {
        printf("caught\n");
    }
    return 0;
}
