/* Test 017: String for-in loop */
#include <stdio.h>

int main() {
    String animals[3] = {"cat", "dog", "fish"};
    printf("for-in: ");
    for (auto s in animals) {
        printf("%s ", s);
    }
    printf("\n");
    return 0;
}