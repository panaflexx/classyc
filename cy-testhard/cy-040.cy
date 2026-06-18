/* Test 040: String array with for-in */
#include <stdio.h>

int main() {
    String fruits[3] = {"apple", "banana", "cherry"};
    printf("for-in string array: ");
    for (auto f in fruits) {
        printf("%s ", f);
    }
    printf("\n");
    return 0;
}