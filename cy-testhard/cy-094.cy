/* Test 094: Class with const and volatile */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    int getX() const {
        return this.x;
    }
    
    int getY() volatile {
        return this.y;
    }
};

int main() {
    const Point p(5, 10);
    volatile Point q(3, 7);
    printf("const volatile: %d, %d\n", p.getX(), q.getY());
    return 0;
}