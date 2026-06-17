/* test-interface.c — Phase 1: interface declaration + structural `impl` check.
 *
 * Scope of this test (Phase 1 ONLY):
 *   - declaring an `interface`,
 *   - a class that opts in with `impl` and conforms,
 *   - a class WITHOUT `impl` that has the same methods (a normal class),
 *   - direct method calls.
 *
 * There is intentionally NO Any<I> and NO View builder here yet — those are
 * Phases 2 and 3.  Conformance is structural; `impl` only asks for an early
 * check at the class definition.
 */
#include <stdio.h>

interface Greeter {
    void greet();
    int  rank();
}

/* Opt-in conformance: the compiler verifies Robot satisfies Greeter HERE. */
class Robot impl Greeter {
    int id;
    Robot(int id) { this.id = id; }
    void greet() { printf("beep boop #%d\n", this.id); }
    int  rank()  { return this.id; }
};

/* No `impl` — still a perfectly normal class that happens to have the same
   methods.  Phase 1 does not erase or wrap it, so nothing special happens;
   it just compiles and runs like any class. */
class Human {
    String name;
    Human(String name) { this.name = name; }
    void greet() { printf("hi, I'm %s\n", this.name); }
    int  rank()  { return 1; }
};

int main() {
    printf("=== interface / impl (Phase 1) ===\n\n");

    Robot* r = new Robot(7);
    defer delete r;
    r->greet();
    printf("robot rank = %d\n", r->rank());

    Human* h = new Human("Ada");
    defer delete h;
    h->greet();
    printf("human rank = %d\n", h->rank());

    printf("\nDone.\n");
    return 0;
}
