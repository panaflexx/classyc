#include <stdio.h>

class Point {
    int x, y;

    /* primary constructor */
    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }

    /* destructor: invoked by `delete` (and by `defer delete`) */
    ~Point() { printf("~Point(%d, %d)\n", this.x, this.y); }

    int sum() { return this.x + this.y; }

    /* fluent setters return the object for chaining */
    Point* withX(int v) { this.x = v; return this; }
    Point* withY(int v) { this.y = v; return this; }
};

void main() {
    /* `new Point(...)` heap-allocates and yields a `Point *`, so an array of
       `new` objects is an array of pointers: `Point *[]`.  (Writing the element
       type as a by-value `Point` would be a type error -- the compiler now
       explains how to fix it.) */
    Point* points[] = { new Point(3,1), new Point(1,4), new Point(2,2) };
	int count = sizeof(points) / sizeof(points[0]);
	defer { for (int i = 0; i < count; i++) delete points[i]; } // defer cleanup

    /* element count via the standard sizeof idiom */
    printf("point count = %d\n", count);

    for (int i = 0; i < count; i++)
        printf("points[%d]->sum() = %d\n", i, points[i]->sum());

    /* fluent chaining: each setter returns a Point* */
    points[0]->withX(10)->withY(20);
    printf("after chaining: points[0]->sum() = %d\n", points[0]->sum());

    /* manual clean up heap objects (each invokes ~Point) */
    //for (int i = 0; i < count; i++)
    //    delete points[i];
}
