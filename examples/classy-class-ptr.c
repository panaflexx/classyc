/* classy-class-ptr.c — demonstrates passing a class pointer to another class constructor.
 *
 * Shows:
 *   - class Bar defined first
 *   - class Foo taking Bar* in its constructor
 *   - creating Bar, then Foo with a pointer to it
 *   - using the received pointer inside Foo
 */

#include <stdio.h>

class Bar {
    int val;
    Bar(int v) { this.val = v; }
    int get() { return this.val; }
};

class Foo {
    Bar *partner;
    Foo(Bar *b) {
        this.partner = b;
    }
    void show() {
        printf("Foo partnered with Bar(%d)\n", this.partner.get());
    }
};

int main() {
    printf("=== classy-class-ptr ===\n");
    Bar *b = new Bar(42);
    Foo *f = new Foo(b);
    f.show();
	printf("Success: class pointer passed and used.\n");
	//by_value_test();
	return 0;
}

/* --- by-value class parameter test (classes at file scope) --- */
class BarVal {
    int v;
    BarVal(int x) { this.v = x; }
    int get() { return this.v; }
};

class FooVal {
    BarVal partner;
    FooVal(BarVal b) {
        this.partner = b;
    }
    void show() {
        printf("FooVal partnered with BarVal(%d)\n", this.partner.get());
    }
};

#if 0
int by_value_test() {
    printf("\n--- by-value class parameter ---\n");
    /* We can only create class instances via `new`, which yields a pointer.
       Attempting to pass that pointer where the constructor expects a by-value
       BarVal should trigger the helpful diagnostic in check_assignment_types. */
    BarVal *tmp = new BarVal(7);
    /* The following should produce a clear error telling the user to use BarVal* instead. */
    FooVal *fv = new FooVal(*tmp);  /* deref to try to get a value (may also fail) */
    fv.show();
    printf("Success: bare class value passed to constructor.\n");
    return 0;
}
#endif
