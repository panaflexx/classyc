/* Test 057: Class with const methods */
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
    
    int getY() const {
        return this.y;
    }
};

int main() {
    const Point p(5, 10);
    printf("const methods: %d, %d\n", p.getX(), p.getY());
    return 0;
}