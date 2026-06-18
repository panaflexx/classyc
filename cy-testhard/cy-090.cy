/* Test 090: Class with static member functions */
#include <stdio.h>

class Math {
    static int add(int a, int b) {
        return a + b;
    }
    
    static int multiply(int a, int b) {
        return a * b;
    }
};

int main() {
    printf("static functions: %d, %d\n", Math::add(5, 3), Math::multiply(4, 6));
    return 0;
}