/* Test 104: Complex switch with ranges and fallthrough */
#include <stdio.h>

int main() {
    for (int i = 0; i <= 15; i++) {
        switch (i) {
            case 0 ... 3:
                printf("%d: small\n", i);
                break;
            case 4 ... 7:
                printf("%d: medium\n", i);
                break;
            case 8 ... 11:
                printf("%d: large\n", i);
                break;
            case 12 ... 15:
                printf("%d: xlarge\n", i);
                break;
            default:
                printf("%d: unknown\n", i);
        }
    }

    // Switch on String (via dict key)
    dict d = { "a": 1, "b": 2, "c": 3 };
    for (auto k in d) {
        switch (k) {
            case "a": printf("got a\n"); break;
            case "b": printf("got b\n"); break;
            case "c": printf("got c\n"); break;
            default: printf("got other: %s\n", k);
        }
    }

    return 0;
}
