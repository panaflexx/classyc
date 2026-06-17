/* Test 064: Class with default parameters */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x = 0, int y = 0) {
        this.x = x;
        this.y = y;
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point p1();
    Point p2(5);
    Point p3(3, 4);
    printf("default parameters: %d, %d, %d\n", p1.sum(), p2.sum(), p3.sum());
    return 0;
}