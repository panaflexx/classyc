#include <stdio.h>

/* Exercises the new statement-level features:
 *   - `defer <stmt>;`            run a statement (LIFO) at enclosing scope exit
 *   - `~ClassName() { ... }`     destructor, lowered to __dtor_<Class>
 *   - `delete ptr;`              run the destructor (if any), then free()
 *   - `defer delete ptr;`        idiomatic scope cleanup for heap locals
 */

class Widget {
    int id;

    Widget(int id) {
        this.id = id;
        printf("  ctor Widget(%d)\n", this.id);
    }

    ~Widget() {
        printf("  dtor Widget(%d)\n", this.id);
    }

    int get() { return this.id; }
};

static void defer_order (void) {
    printf("defer_order: enter\n");
    defer printf("  defer A (runs last)\n");
    defer printf("  defer B\n");
    defer printf("  defer C (runs first)\n");
    printf("defer_order: body done\n");
}

static void defer_return (int early) {
    printf("defer_return(%d): enter\n", early);
    defer printf("  cleanup for %d\n", early);
    if (early) {
        printf("  taking early return\n");
        return;
    }
    printf("  reached end\n");
}

static void defer_loop (void) {
    printf("defer_loop: enter\n");
    for (int i = 0; i < 3; i++) {
        defer printf("  loop defer i=%d\n", i);
        printf("  loop body i=%d\n", i);
        if (i == 1) { printf("  breaking\n"); break; }
    }
    printf("defer_loop: after loop\n");
}

int main (void) {
    defer_order ();
    printf("\n");
    defer_return (0);
    defer_return (1);
    printf("\n");
    defer_loop ();
    printf("\n");

    /* explicit delete: ctor, use, dtor, free */
    printf("explicit delete:\n");
    Widget *w = new Widget(7);
    printf("  w.get() = %d\n", w->get());
    delete w;
    printf("\n");

    /* scope-guard idiom: defer delete runs the dtor + free at scope exit */
    printf("defer delete:\n");
    {
        Widget *a = new Widget(100);
        defer delete a;
        Widget *b = new Widget(200);
        defer delete b;
        printf("  using a=%d b=%d\n", a->get(), b->get());
    }
    printf("  block exited\n");

    /* `defer` and `delete` still usable as ordinary identifiers */
    int defer = 5, delete = 9;
    printf("\nidentifiers: defer=%d delete=%d\n", defer, delete);
    return 0;
}
