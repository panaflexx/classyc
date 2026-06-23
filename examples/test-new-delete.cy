// test-new-delete.cy - exercises new/delete recognition in the ownership pass.
//
//   bin/classyc -I include -fownership-report examples/test-new-delete.cy
//
// What we verify:
//   1. `Point* p = new Point(...)` is tracked (acquire_fn = "new")
//   2. `delete p;` releases it (release_kind = "deleted")
//   3. Forgetting `delete` produces a leak warning
//   4. -fauto-release synthesizes `defer delete p;` for definite leaks
//   5. Returning the pointer detaches (no warning)
//   6. Storing into a class field detaches (no warning)
//   7. Passing to a `take_point` wrapper with no annotation: IP infers
//      ((releases)) and the caller's binding is recognized as freed.

#include <stdio.h>
#include <stdlib.h>

class Point {
    int x;
    int y;
    Point(int x, int y) { this.x = x; this.y = y; }
    ~Point() { /* explicit dtor */ }
};

// Wrapper that takes ownership of a Point* and deletes it.
// IP analysis should infer ((releases)).
void take_point(Point* p) {
    delete p;
}

// Wrapper that just reads. Should infer ((borrows)).
void read_point(Point* p) {
    if (p) printf("p->x=%d\n", p->x);
}

// Wrapper that allocates and returns. Should set returns_owned_p.
Point* make_point(int x, int y) {
    Point* p = new Point(x, y);
    return p;
}

// (1) Explicit delete - no warning expected.
void cleanly_deleted() {
    Point* a = new Point(1, 2);
    delete a;
}

// (2) Leak - no delete, no escape.
void definitely_leaks() {
    Point* b = new Point(3, 4);
    printf("b->x=%d\n", b->x);
}

// (3) Returned to caller - no warning.
Point* returns_owned() {
    Point* c = new Point(5, 6);
    return c;
}

// (4) Stored into a class field - escape.
class Container {
    Point* head;

    void take(int x, int y) {
        Point* fresh = new Point(x, y);
        this.head = fresh;
    }
};

// (5) Wrapper-call recognition.  Without IP, this looks like a leak (the
// call escapes ownership of `d`).  With IP analysis we should infer
// take_point ((releases)) and report `freed by release fn`.
void via_wrapper() {
    Point* d = new Point(7, 8);
    take_point(d);
}

// (6) Borrowing-wrapper - the call doesn't take ownership; the binding
// should still leak.
void via_borrow() {
    Point* e = new Point(9, 10);
    read_point(e);
}

int main() {
    cleanly_deleted();
    definitely_leaks();
    Point* r = returns_owned();
    delete r;
    Container* cont = new Container();
    cont->take(11, 12);
    delete cont->head;  // user manually cleans up the stored field
    delete cont;
    via_wrapper();
    via_borrow();
    return 0;
}
