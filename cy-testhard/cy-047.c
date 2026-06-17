/* Test 047: Class with virtual destructor */
#include <stdio.h>

class Base {
    virtual ~Base() {
        printf("Base destroyed\n");
    }
};

class Derived : Base {
    ~Derived() {
        printf("Derived destroyed\n");
    }
};

int main() {
    Base *b = new Derived();
    delete b;
    return 0;
}