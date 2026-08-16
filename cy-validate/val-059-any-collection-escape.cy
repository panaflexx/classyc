/* val-059-any-collection-escape.cy — Any<I>* handles retained in a collection
 * must not be freed by the object arena while the collection still holds them.
 *
 * Two variants, both previously a use-after-free:
 *
 *   (a) a function builds a List<Any<I>*> and returns it: the function-level
 *       object-arena release used to fire at return and free every handle
 *       still sitting in the returned list, before the caller ever reads it.
 *
 *   (b) a loop retains a freshly-built handle into a collection declared
 *       outside the loop (`outer->Add(any<I>(...))`): the PER-ITERATION
 *       object-arena release used to fire at the bottom of each iteration and
 *       free the handle that iteration just added, corrupting the collection
 *       before the loop even finished -- no return needed to trigger it.
 *
 * Fix: subtree_retains_object_in_collection_p (src/classyc.c) detects a
 * handle passed as an argument to a method call on a generic collection
 * specialized on an Any<I> handle type, and disables BOTH the per-iteration
 * loop arena and the function-level arena for the enclosing function/loop.
 * Handles built by such a function/loop fall back to ordinary owned-pointer
 * semantics -- the collection needs `.owns()` / `.ownsValues()` (or manual
 * per-element delete) to free them, same as any other List<T*>.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-059-any-collection-escape.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}
int approx(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 0.001; }

interface Shape { double area(); }

class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};

class Circle {
    double r;
    Circle(double r) { this.r = r; }
    double area() { return 3.14159 * r * r; }
    ~Circle() {}
};

/* (a) builds and returns a List<Any<Shape>*>* -- handles retained inline. */
List<Any<Shape>*>* makeShapes() {
    auto lst = new List<Any<Shape>*>();
    lst->owns();
    lst->Add(any<Shape>(new Square(2.0)));
    lst->Add(any<Shape>(new Square(3.0)));
    return lst;
}

int main() {
    printf("=== val-059 Any<I>* collection escape ===\n\n");

    /* (a) collection returned from a function */
    {
        List<Any<Shape>*>* shapes = makeShapes();
        double total = 0;
        for (auto h in shapes) total += h->area();
        check(approx(total, 13.0), "(a) collection of handles survives function return");
        delete shapes;   /* .owns() -> deletes each handle; must not crash */
        check(1, "(a) delete of returned owning collection did not crash");
    }

    /* (b) loop retains an inline any<I>() into a collection declared outside
       the loop -- must not be freed at the per-iteration release. */
    {
        List<Any<Shape>*>* shapes = new List<Any<Shape>*>();
        shapes->owns();
        for (int i = 1; i <= 4; i++) {
            shapes->Add(any<Shape>(new Circle((double)i)));
        }
        double total = 0;
        for (auto s in shapes) total += s->area();
        check(approx(total, 3.14159 * (1 + 4 + 9 + 16)),
              "(b) handle added inside a loop survives past its own iteration");
        delete shapes;
        check(1, "(b) delete of loop-built owning collection did not crash");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
