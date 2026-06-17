/* Test 060: Class with friend class */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    friend class Calculator;
};

class Calculator {
    int calculate(Point p) {
        return p.x + p.y;
    }
};

int main() {
    Point p(3, 4);
    Calculator c;
    printf("friend class: %d\n", c.calculate(p));
    return 0;
}