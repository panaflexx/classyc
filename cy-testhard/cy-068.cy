/* Test 068: Class with multiple inheritance (simplified) */
#include <stdio.h>

class A {
    int a;
    
    A(int a) {
        this.a = a;
    }
    
    int getA() {
        return this.a;
    }
};

class B {
    int b;
    
    B(int b) {
        this.b = b;
    }
    
    int getB() {
        return this.b;
    }
};

class C : A, B {
    int c;
    
    C(int a, int b, int c) : A(a), B(b) {
        this.c = c;
    }
    
    int getC() {
        return this.c;
    }
};

int main() {
    C c(1, 2, 3);
    printf("multiple inheritance: %d, %d, %d\n", c.getA(), c.getB(), c.getC());
    return 0;
}