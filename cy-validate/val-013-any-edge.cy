/* val-013-any-edge.cy — pushes interfaces + Any<I> past the basics.
 *
 * Validates (all PASS):
 *   · multi-method interface with NON-void returns (double, String) + an arg
 *   · mutation through an erased handle (scale)
 *   · passing a handle to a function and returning one from a function
 *   · a mix of opt-in (`impl`) and purely structural conformance
 *   · heterogeneous Map<String, Any<Shape>*> and List<Any<Shape>*> (for-in)
 *   · NESTED composition: a class that stores an Any<Shape>* and delegates
 *   · the SAME concrete class erased to TWO different interfaces (SHORTCOMINGS
 *     E1, now fixed): per-class thunks are emitted once and re-declared after
 *
 * Previously a KNOWN BUG (SHORTCOMINGS.md E1): erasing the same class to two
 * different interfaces aborted with "Repeated item declaration
 * __thunk_dtor_<Class>". That is now fixed and validated below.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-013-any-edge.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}
int approx(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 0.001; }

interface Shape {
    double area();
    String name();
    void   scale(double f);
}

/* A second interface that reuses the name() method (shares __thunk_name_<C>)
   so erasing one class to both interfaces exercises the per-class thunk
   dedup fixed for SHORTCOMINGS.md E1. */
interface Named {
    String name();
}

class Circle impl Shape {              /* opt-in conformance */
    double r;
    Circle(double r) { this.r = r; }
    double area() { return 3.14159 * this.r * this.r; }
    String name() { return "circle"; }
    void   scale(double f) { this.r = this.r * f; }
    ~Circle() {}
};

class Square {                          /* structural conformance (no impl) */
    double s;
    Square(double s) { this.s = s; }
    double area() { return this.s * this.s; }
    String name() { return "square"; }
    void   scale(double f) { this.s = this.s * f; }
    ~Square() {}
};

/* A class that COMPOSES an erased handle and delegates through it. */
class Frame {
    Any<Shape>* inner;
    String      title;
    Frame(String title, Any<Shape>* inner) { this.title = title; this.inner = inner; }
    double inner_area() { return this.inner->area(); }   /* delegate via handle */
    ~Frame() {}
};

/* Free functions over the erased type. */
double area_of(Any<Shape>* h) { return h->area(); }
Any<Shape>* bigger_circle(double r) { return any<Shape>(new Circle(r)); }

int main() {
    printf("=== val-013 Any<I> edge cases ===\n\n");

    /* non-void returns + String return through the erased handle */
    Any<Shape>* c = any<Shape>(new Circle(2.0));
    check(approx(c->area(), 12.56636), "erased non-void (double) return");
    check(strcmp((char*)c->name(), "circle") == 0, "erased String return");

    /* method with an argument mutates the wrapped object */
    c->scale(2.0);
    check(approx(c->area(), 50.26544), "erased method with arg mutates state");

    /* structural conformance dispatches correctly */
    Any<Shape>* sq = any<Shape>(new Square(3.0));
    check(approx(sq->area(), 9.0) && strcmp((char*)sq->name(), "square") == 0,
          "structural (no-impl) class dispatches via Any");

    /* pass handle to a function / return handle from a function */
    check(approx(area_of(sq), 9.0), "pass Any<Shape>* into a function");
    Any<Shape>* mc = bigger_circle(5.0);
    check(approx(mc->area(), 78.53975), "return Any<Shape>* from a function");

    /* heterogeneous List<Any<Shape>*> */
    List<Any<Shape>*>* shapes = new List<Any<Shape>*>();
    defer delete shapes;
    shapes->Add(any<Shape>(new Circle(1.0)));
    shapes->Add(any<Shape>(new Square(2.0)));
    shapes->Add(any<Shape>(new Circle(3.0)));
    double lt = 0; for (auto s in shapes) lt += s->area();
    check(approx(lt, 3.14159 + 4.0 + 28.27431), "List<Any<Shape>*> for-in sums areas");

    /* heterogeneous Map<String, Any<Shape>*> */
    Map<String, Any<Shape>*>* byName = new Map<String, Any<Shape>*>();
    defer delete byName;
    byName["unit-circle"] = any<Shape>(new Circle(1.0));
    byName["2x2-square"]  = any<Shape>(new Square(2.0));
    double mt = 0; for (auto k, v in byName) mt += v->area();
    check(approx(mt, 3.14159 + 4.0), "Map<String,Any<Shape>*> for-in sums areas");
    check(approx(byName["2x2-square"]->area(), 4.0), "Map subscript -> erased method call");

    /* nested composition: Frame holds an Any<Shape>* and delegates */
    Frame* f = new Frame("framed", any<Shape>(new Circle(2.0)));
    defer delete f;
    check(approx(f->inner_area(), 12.56636), "class composing Any<Shape>* delegates correctly");

    /* same concrete class behind two handles (distinct objects) */
    Any<Shape>* s1 = any<Shape>(new Square(2.0));
    Any<Shape>* s2 = any<Shape>(new Square(5.0));
    check(approx(s1->area(), 4.0) && approx(s2->area(), 25.0),
          "two independent handles of the same class don't alias");

    /* SHORTCOMINGS.md E1 (FIXED): erase the SAME concrete class to TWO
       different interfaces. The forwarding/destructor thunks are now emitted
       once per class (a repeat erasure only re-declares them), so this no
       longer aborts with "Repeated item declaration __thunk_dtor_<Class>".
       Note `Named` shares the name() method with `Shape`, exercising the
       shared-thunk path too. */
    Any<Shape>* dual_shape = any<Shape>(new Square(2.0));   /* Square -> Shape  */
    Any<Named>* dual_named = any<Named>(new Square(7.0));   /* Square -> Named  */
    check(approx(dual_shape->area(), 4.0), "same class erased to interface #1 (Shape)");
    check(strcmp((char*)dual_named->name(), "square") == 0,
          "same class erased to interface #2 (Named) — E1 fixed");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
