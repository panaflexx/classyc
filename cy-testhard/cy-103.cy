/* Test 103: Defer with early returns and multiple scopes */
#include <stdio.h>

int counter = 0;

void cleanup(const char* name) {
    counter++;
    printf("cleanup %s (%d)\n", name, counter);
}

int test_early_return(int which) {
    defer cleanup("A");
    if (which == 1) return 1;
    defer cleanup("B");
    if (which == 2) return 2;
    defer cleanup("C");
    return 3;
}

int main() {
    printf("test 1:\n"); test_early_return(1); counter = 0;
    printf("test 2:\n"); test_early_return(2); counter = 0;
    printf("test 3:\n"); test_early_return(3); counter = 0;

    // Nested scopes with defer
    {
        defer cleanup("outer1");
        {
            defer cleanup("inner1");
            printf("inner scope\n");
        }
        printf("after inner\n");
    }
    printf("after outer\n");

    return 0;
}
