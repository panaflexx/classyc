/* Test 044: Class with method overloading */
#include <stdio.h>

class Calculator {
    int value;
    
    Calculator(int value) {
        this.value = value;
    }
    
    int add(int x) {
        return this.value + x;
    }
    
    int add(int x, int y) {
        return this.value + x + y;
    }
};

int main() {
    Calculator c(10);
    printf("method overloading: %d, %d\n", c.add(5), c.add(2, 3));
    return 0;
}