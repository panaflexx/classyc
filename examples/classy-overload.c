/* classy-overload.c — class method name mangling and overloading.
 *
 * Class methods are lowered to free functions whose symbol name encodes the
 * class, the method, and the (user) parameter types, e.g.
 *
 *     Vec::add(int)         ->  Vec_add__i
 *     Vec::add(int,int)     ->  Vec_add__ii
 *     Vec::add(double)      ->  Vec_add__d
 *
 * This avoids global-symbol collisions in AOT output (previously a method like
 * `withX` was emitted as a bare global `withX`) and lets several methods of one
 * class share a name, distinguished by their parameter types (overloading).
 * The best overload is selected from the argument types at each call site.
 */

#include <stdio.h>
#include <string.h>

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed = passed + 1; }
    else      { printf("  FAIL  %s\n", label); failed = failed + 1; }
}

class Vec {
    int x, y;

    Vec(int x, int y) { this.x = x; this.y = y; }

    /* overloaded by arity */
    int sum() { return this.x + this.y; }
    int sum(int bias) { return this.x + this.y + bias; }

    /* overloaded by parameter type */
    int scale(int k)    { return this.x * k; }
    double scale(double k) { return this.x * k; }

    /* overloaded by arity AND type */
    Vec* set(int v)         { this.x = v; this.y = v; return this; }
    Vec* set(int a, int b)  { this.x = a; this.y = b; return this; }
};

/* A different class with a same-named method: must not collide with Vec::sum. */
class Bag {
    int n;
    Bag(int n) { this.n = n; }
    int sum() { return this.n * 100; }
};

int main() {
    printf("=== method overloading test suite ===\n\n");
    passed = 0;
    failed = 0;

    Vec* v = new Vec(3, 4);

    /* overload by arity */
    check(v->sum() == 7,        "1a  sum()        -> Vec_sum__v");
    check(v->sum(10) == 17,     "1b  sum(int)     -> Vec_sum__i");

    /* overload by parameter type (int vs double) */
    check(v->scale(2) == 6,             "2a  scale(int)    -> int result 6");
    check(v->scale(2.5) > 7.4 && v->scale(2.5) < 7.6,
                                        "2b  scale(double) -> double result 7.5");

    /* overload by arity and type, with fluent chaining */
    v->set(5);
    check(v->sum() == 10,       "3a  set(int) sets both -> sum 10");
    v->set(1, 2);
    check(v->sum() == 3,        "3b  set(int,int) -> sum 3");

    /* same-named method in a different class resolves independently */
    Bag* b = new Bag(2);
    check(b->sum() == 200,      "4a  Bag::sum is not Vec::sum");
    check(v->set(4, 6)->sum() == 10, "4b  chained set(int,int) then sum");

    /* value receiver also resolves overloads */
    Vec copy = *v;
    check(copy.sum(100) == 110, "5a  value-receiver overloaded call");

    delete v;
    delete b;

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
