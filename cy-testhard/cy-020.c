/* Test 020: Heap allocation with new */
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
    Point *p = new Point(1, 2);
    printf("heap: %d\n", p->sum());
    delete p;
    return 0;
}