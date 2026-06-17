/* Test 083: Class with virtual functions */
#include <stdio.h>

class Base {
    virtual int getValue() {
        return 10;
    }
};

class Derived : Base {
    int getValue() {
        return 20;
    }
};

int main() {
    Base *b = new Derived();
    printf("virtual functions: %d\n", b->getValue());
    delete b;
    return 0;
}