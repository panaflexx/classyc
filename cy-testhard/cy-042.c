/* Test 042: Class with multiple constructors */
#include <stdio.h>

class Point {
    int x, y;
    
    Point() {
        this.x = 0;
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
    Point p1();
    Point p2(5, 10);
    printf("multiple constructors: %d, %d\n", p1.sum(), p2.sum());
    return 0;
}