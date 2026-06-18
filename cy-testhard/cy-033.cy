/* Test 033: Class inheritance (simplified) */
#include <stdio.h>

class Base {
    int value;
    
    Base(int value) {
        this.value = value;
    }
    
    int getValue() {
        return this.value;
    }
};

class Derived : Base {
    int extra;
    
    Derived(int value, int extra) : Base(value) {
        this.extra = extra;
    }
    
    int getExtra() {
        return this.extra;
    }
};

int main() {
    Derived d(10, 20);
    printf("inheritance: %d + %d\n", d.getValue(), d.getExtra());
    return 0;
}