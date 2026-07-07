/* bugs/002-asit-as-drawable-dmul-crash.cy
 *
 * FIXED.  Root cause: the self-reference check in get_or_create_specialization
 * only handled self-references (List<T> inside List<T>), not cross-references
 * (Is<T> inside As<T>).  When As<Drawable> was specialized, Is<T> was parsed as
 * a real specialization (__generic_Is_T) instead of a placeholder, corrupting
 * later codegen.
 *
 * Fix: extended the self-reference check to also match cross-references where
 * all type args are params of the CURRENT class (not just the matched template).
 * Cross-refs are recorded in generic_crossrefs and materialized after the outer
 * specialize_node returns, avoiding recursive get_or_create_specialization.
 *
 * Note: As<I>.Of for interface T returns the raw concrete pointer, not a
 * re-erased Any<I>* handle.  For interface T, use Is<I>.Of + direct cast.
 */
#include "include/asit.h"
#include <stdio.h>

interface Shape { double area(); }
interface Drawable { void draw(); }

class Circle {
    double r;
    Circle(double r) { this.r = r; }
    double area() { return 3.14159 * this.r * this.r; }
    void draw() { printf("Circle\n"); }
};

int main() {
    Any<Shape>* s1 = any<Shape>(new Circle(2.0));
    asit_register_type(s1, "Circle");
    Any<Drawable>* d = (Any<Drawable>*)As<Drawable>.Of(s1);
    printf("d = %p\n", (void*)d);
    return 0;
}
