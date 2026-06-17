/* Test 018: Class definition with constructor */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point p(3, 4);
    printf("class: %d\n", p.sum());
    return 0;
}