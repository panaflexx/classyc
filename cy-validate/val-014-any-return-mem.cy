/* val-014-any-return-mem.cy — regression test for the object-arena return fix
 * (SHORTCOMINGS.md E0). Returning an Any<I> handle from a function must hand the
 * caller a LIVE object (not a freed/zeroed one), and method calls on it must
 * read correct state. Also exercises returning through several frames, storing
 * the returned handle in a collection, and an alloc loop for leak sanity.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-014-any-return-mem.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}
int approx(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 0.001; }

interface Shape { double area(); String name(); }

class Circle impl Shape {
    double r;
    Circle(double r) { this.r = r; }
    double area() { return 3.14159 * this.r * this.r; }
    String name() { return "circle"; }
    ~Circle() {}
};

/* directly returns a freshly-erased handle */
Any<Shape>* make_circle(double r) { return any<Shape>(new Circle(r)); }

/* returns a handle through an extra call frame */
Any<Shape>* make_circle_2(double r) { return make_circle(r); }

/* returns a handle bound to a local first (return <var>, not return <expr>) */
Any<Shape>* make_named(double r) {
    Any<Shape>* h = any<Shape>(new Circle(r));
    return h;
}

int main() {
    printf("=== val-014 Any<I> return memory ===\n\n");

    /* 1. direct return then method call — was use-after-free (0.0/segfault) */
    Any<Shape>* a = make_circle(5.0);
    check(approx(a->area(), 78.53975), "returned handle has LIVE state (area)");
    check(strcmp((char*)a->name(), "circle") == 0, "returned handle String method ok");

    /* 2. nested as a call argument (the form that crashed in val-013) */
    check(approx(make_circle(2.0)->area(), 12.56636), "return value method as nested call arg");

    /* 3. return through two frames */
    Any<Shape>* b = make_circle_2(3.0);
    check(approx(b->area(), 28.27431), "handle survives through two return frames");

    /* 4. return a locally-bound handle */
    Any<Shape>* c = make_named(1.0);
    check(approx(c->area(), 3.14159), "return <local var> handle survives");

    /* 5. store returned handles in a collection, use later */
    List<Any<Shape>*>* shapes = new List<Any<Shape>*>();
    defer delete shapes;
    for (int i = 1; i <= 4; i++) shapes->Add(make_circle((double)i));
    double total = 0; for (auto s in shapes) total += s->area();
    check(approx(total, 3.14159 * (1 + 4 + 9 + 16)), "collection of returned handles intact");

    /* 6. alloc loop: many returned handles, last one still correct (leak sanity) */
    int ok = 1;
    for (int i = 0; i < 20000; i++) {
        Any<Shape>* h = make_circle(2.0);
        if (i == 19999 && !approx(h->area(), 12.56636)) ok = 0;
        delete h;   /* caller owns the returned handle now */
    }
    check(ok == 1, "20k returned-handle alloc/delete loop stays correct");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
