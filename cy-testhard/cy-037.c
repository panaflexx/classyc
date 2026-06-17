/* Test 037: Class with virtual methods (concept) */
#include <stdio.h>

class Shape {
    virtual int area() {
        return 0;
    }
};

class Rectangle : Shape {
    int width, height;
    
    Rectangle(int width, int height) {
        this.width = width;
        this.height = height;
    }
    
    int area() {
        return this.width * this.height;
    }
};

int main() {
    Rectangle r(5, 10);
    printf("area: %d\n", r.area());
    return 0;
}