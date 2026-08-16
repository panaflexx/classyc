/* val-060-owns-warning.cy — compile-time reminder for a pointer-holding
 * collection that's never marked .owns()/.ownsValues()/.ownsKeys().
 *
 * This is the compiler check requested after examples/test-any-arena.cy and
 * examples/test-any-implicit.cy crashed at process exit: both built a
 * List<Any<I>*> via Add(any<I>(...)) without ever calling .owns() on it.
 * ownership.c now warns on that exact shape at compile time instead of
 * letting it silently leak (and, for Any<I> handles specifically, crash at
 * exit via the still-open atexit cleanup bug -- see SHORTCOMINGS.md).
 *
 * This file is a *compile-only* smoke test: it doesn't assert on warning
 * text (that's brittle), just that the compiler still accepts and runs
 * correct code, both the "flag it" and the three "don't flag it" shapes.
 * The actual warning-text checks were done by hand while building the
 * feature (see cy-validate/SHORTCOMINGS.md gotcha "unowned pointer
 * collection reminder").
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-060-owns-warning.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int approx_eq (double x, double y) { double d = x - y; if (d < 0) d = -d; return d < 0.001; }

interface Shape { double area(); }

class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};

int main() {
    printf("=== val-060 unowned-collection warning shapes ===\n\n");

    /* (a) .owns() called separately, after declaration -- no warning, and
       elements are correctly freed by the collection's own destructor. */
    {
        List<Any<Shape>*>* a = new List<Any<Shape>*>();
        a->owns();
        a->Add(any<Shape>(new Square(2.0)));
        check(approx_eq (a->Get(0)->area(), 4.0), "(a) separate .owns() -- element correct");
        delete a;
    }

    /* (b) .owns() chained directly onto `new` in the initializer -- no
       warning (this is the idiom cy-validate/val-017 already uses). */
    {
        List<Any<Shape>*>* b = new List<Any<Shape>*>().owns();
        b->Add(any<Shape>(new Square(3.0)));
        check(approx_eq (b->Get(0)->area(), 9.0), "(b) chained .owns() -- element correct");
        delete b;
    }

    /* (c) no .owns() at all, but every element is deleted by hand via
       for-in -- no warning (val-012's idiom), and no leak/crash either. */
    {
        List<Any<Shape>*>* c = new List<Any<Shape>*>();
        c->Add(any<Shape>(new Square(5.0)));
        c->Add(any<Shape>(new Square(6.0)));
        double total = 0;
        for (auto h in c) total += h->area();
        check(approx_eq (total, 25.0 + 36.0), "(c) manual for-in delete -- elements correct");
        for (auto h in c) delete h;
        delete c;
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
