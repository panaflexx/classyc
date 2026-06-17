/* Test 024: f-string interpolation */
#include <stdio.h>

int main() {
    String name = "Alice";
    int age = 30;
    String message = f"Hello {name}, you are {age} years old";
    printf("f-string: %s\n", message);
    return 0;
}