/* Test 051: Class with constructor chaining */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x) {
        this.x = x;
        this.y = 0;
    }
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point p1(5);
    Point p2(3, 4);
    printf("constructor chaining: %d, %d\n", p1.sum(), p2.sum());
    return 0;
}