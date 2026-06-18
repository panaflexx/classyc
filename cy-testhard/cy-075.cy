/* Test 075: Class with operator overloading (concept) */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    Point operator+(Point other) {
        return Point(this.x + other.x, this.y + other.y);
    }
    
    int getX() {
        return this.x;
    }
    
    int getY() {
        return this.y;
    }
};

int main() {
    Point p1(1, 2);
    Point p2(3, 4);
    Point p3 = p1 + p2;
    printf("operator overloading: %d, %d\n", p3.getX(), p3.getY());
    return 0;
}