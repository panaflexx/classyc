/* Test 021: Fluent chaining */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    Point* withX(int x) {
        this.x = x;
        return this;
    }
    
    Point* withY(int y) {
        this.y = y;
        return this;
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point *p = new Point(0, 0).withX(10).withY(20);
    printf("chaining: %d\n", p->sum());
    delete p;
    return 0;
}