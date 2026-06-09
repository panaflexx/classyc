#include <stdio.h>

/* Exercises the new heap-allocation + constructor support:
 *   - `new ClassName(args)`  -> heap allocation + constructor call
 *   - implicit `this` in the constructor body
 *   - comma-separated data members (int x, y;)
 *   - method-call chaining on a new-expression
 *   - Go-style "return this" fluent chaining
 *   - named constructor arguments (any order)
 *   - `~ClassName()` destructor + `delete obj`
 *   - `defer <stmt>` for scope-exit cleanup (LIFO), e.g. `defer delete obj;`
 */

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

int main() {
    /* 1. plain heap allocation + constructor */
    Point *p = new Point(2, 5);
    defer delete p; /* runs the destructor + frees p when main() exits */
    printf("p:        x=%d y=%d\n", p->x, p->y);

    /* 2. distinct heap objects */
    Point *q = new Point(40, 2);
    defer delete q;
    printf("q:        x=%d y=%d\n", q->x, q->y);

    /* 3. method call chained directly on a new-expression */
    printf("sum:      %d\n", new Point(3, 4).sum());

    /* 4. Go-style fluent chaining (each setter returns this) */
    Point *fluent = new Point(0, 0).withX(11).withY(22);
    defer delete fluent;
    printf("fluent:   x=%d y=%d\n", fluent->x, fluent->y);

    /* 5. named arguments, in declared order */
    Point *n1 = new Point(x=5, y=2);
    defer delete n1;
    printf("named1:   x=%d y=%d\n", n1->x, n1->y);

    /* 6. named arguments, out of order */
    Point *n2 = new Point(y=99, x=7);
    defer delete n2;
    printf("named2:   x=%d y=%d\n", n2->x, n2->y);

    /* the five `defer delete`s above now run here, in reverse order:
       n2, n1, fluent, q, p */
    printf("--- main exiting, deferred cleanup runs now ---\n");
    return 0;
}
