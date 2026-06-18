/* Test 055: Class with friend functions */
#include <stdio.h>

class Point {
    int x, y;
    
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    
    friend int getSum(Point p) {
        return p.x + p.y;
    }
};

int main() {
    Point p(3, 4);
    printf("friend function: %d\n", getSum(p));
    return 0;
}