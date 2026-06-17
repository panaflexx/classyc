/* Test 022: Named constructor parameters */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(x, y) {
        this.x = x;
        this.y = y;
    }
    
    int sum() {
        return this.x + this.y;
    }
};

int main() {
    Point *p = new Point(x=5, y=10);
    printf("named params: %d\n", p->sum());
    delete p;
    return 0;
}