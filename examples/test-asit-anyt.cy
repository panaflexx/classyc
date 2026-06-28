/* test-asit-anyt.cy — Is<T> / As<T> using ClassyC generics syntax (header)
 *
 * Exercises the compiler's two new expressiveness primitives:
 *   - Generic class instantiation in expression context (Is<T>.Of(h))
 *   - nameof<T>() compile-time reflection
 *
 * And the header's Is<T>/As<T> generic classes, which use those primitives
 * to implement concrete-T and interface-T type tests against a side RTTI
 * registry populated by asit_register_type / asit_impl.
 */
#include "include/asit.h"
#include <stdio.h>

interface Shape   { double area(); }
interface Drawable { void draw(); }

class Circle {
    double r;
    Circle(double r) { this.r = r; }
    double area() { return 3.14159 * this.r * this.r; }
    void draw()   { printf("Circle r=%.1f\n", this.r); }
};

class Square {
    double side;
    Square(double s) { this.side = s; }
    double area() { return this.side * this.side; }
};

/* Declare the interface conformance of each concrete class for the
 * header-side RTTI table (mirrors the `impl` keyword, but at runtime). */
int main() {
    asit_impl(Circle, Shape);
    asit_impl(Circle, Drawable);
    asit_impl(Square, Shape);
    int passed = 0, failed = 0;
    #define CHECK(c,msg) if(c){printf("PASS %s\n",msg);passed++;}else{printf("FAIL %s\n",msg);failed++;}

    Any<Shape>* s1 = any<Shape>(new Circle(2.0));
    asit_register_type(s1, "Circle");
    Any<Shape>* s2 = any<Shape>(new Square(3.0));
    asit_register_type(s2, "Square");

    /* concrete type tests — Is<T>.Of / As<T>.Of (generic class syntax) */
    CHECK( Is<Circle>.Of(s1),  "Is<Circle>.Of true for Circle");
    CHECK(!Is<Square>.Of(s1),  "Is<Square>.Of false for Circle");
    CHECK( Is<Square>.Of(s2),  "Is<Square>.Of true for Square");

    /* interface conformance — Is<I>.Of (generic class syntax) */
    CHECK( Is<Shape>.Of(s1),    "Is<Shape>.Of true");
    CHECK( Is<Drawable>.Of(s1), "Is<Drawable>.Of true for Circle");
    CHECK(!Is<Drawable>.Of(s2), "Is<Drawable>.Of false for Square");

    /* As<T>.Of — raw pointer (generic class syntax).  Returns void* so the
       same class works for both concrete-T and interface-T; caller casts. */
    Circle* c = (Circle*)As<Circle>.Of(s1);
    CHECK(c && c->area() > 12.0, "As<Circle>.Of yields working object");
    CHECK(As<Square>.Of(s1) == NULL, "As<Square>.Of on Circle yields NULL");

    printf("area via As<Circle>.Of = %.2f\n", c->area());

    /* Interface handle: Is<I>.Of checks conformance, then the original handle
       is re-typed as Any<I>* (the concrete object satisfies I structurally, so
       the same handle's vtable slots work for I's methods too).  As<I>.Of is
       not used here because it returns the raw concrete pointer, not a handle. */
    CHECK(Is<Drawable>.Of(s1), "Is<Drawable>.Of true for Circle");
    Any<Drawable>* d = (Any<Drawable>*)s1;
    d->draw();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
