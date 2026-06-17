/* Test 079: Class with constructor delegation */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    Point(int value) : Point(value, value) {
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point p1(5, 10);
    Point p2(7);
    printf("constructor delegation: %d, %d\n", p1.sum(), p2.sum());
    return 0;
}