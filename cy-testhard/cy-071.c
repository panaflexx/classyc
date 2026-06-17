/* Test 071: Class with template-like behavior (using macros) */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    Point* move(int dx, int dy) {
        this.x += dx;
        this.y += dy;
        return this;
    }
    
    int getX() {
        return this.x;
    }
    
    int getY() {
        return this.y;
    }
};

int main() {
    Point *p = new Point(1, 2);
    p->move(3, 4);
    printf("template-like: %d, %d\n", p->getX(), p->getY());
    delete p;
    return 0;
}