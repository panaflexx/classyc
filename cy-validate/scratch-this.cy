#include <stdio.h>

// Mirrors the README's Point example after the cleanup, to lock the
// "bare field access is fine; this. only needed for disambiguation" rule.

class Point {
    int x, y;

    Point(int x, int y) { this.x = x; this.y = y; }   // disambiguates field from parameter
    ~Point() { printf("~Point(%d,%d)\n", x, y); }     // bare reads

    Point* withX(int v) { x = v; return this; }       // bare write + `this` pronoun
    int sum() { return x + y; }                       // bare reads
};

int main() {
    Point* p = new Point(3, 4).withX(10);
    defer delete p;
    printf("x=%d y=%d sum=%d\n", p->x, p->y, p->sum());
    return 0;
}
