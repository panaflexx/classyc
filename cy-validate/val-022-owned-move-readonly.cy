/* val-022-owned-move-readonly.cy — the opt-in managed-ownership layer.
 *
 * Exercises the `owned` / `move` / `readonly` keywords (the GC-like layer that
 * sits on top of unmanaged C11 / plain new-delete):
 *
 *   owned auto x = new Box(...);   // single-owner, auto-released at scope exit
 *   auto y = move x;               // ownership x -> y; x is now a read-only view
 *   auto z = readonly y;           // non-owning read-only view
 *
 * Each scope's owner is released exactly once (verified with a destructor
 * counter), moved-from views are never double-freed, and readonly borrows
 * have no effect on ownership. */
#include <stdio.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int dtors = 0;
class Box {
    int v;
    Box(int v) { this->v = v; }
    ~Box() { dtors++; }
    int get() { return this->v; }
};

int main() {
    printf("=== val-022 owned / move / readonly ===\n\n");

    /* (1) An `owned` binding is auto-released exactly once at scope exit, with
     *     no manual `delete` and no leak. */
    {
        owned auto b = new Box(11);
        check(b->v == 11, "(1) owned binding readable");
        check(dtors == 0, "(1) not released before scope exit");
    }
    check(dtors == 1, "(1) owned auto-released exactly once at scope exit");

    /* (2) `move` CONSUMES the source: ownership transfers to the target, the
     *     source binding is dead (any use is a compile error), and exactly one
     *     release happens (no double free).  To keep reading the object, use
     *     the new owner `y` (or take a `readonly` before moving). */
    dtors = 0;
    {
        owned auto x = new Box(22);
        auto y = move x;                 // ownership x -> y; x is now dead
        // NOTE: `x->v` here would be a compile error (use of moved value).
        check(y->v == 22, "(2) move target holds the value");
        check(dtors == 0, "(2) nothing released yet");
    }
    check(dtors == 1, "(2) released exactly once (no double free)");

    /* (3) `readonly` borrows a non-owning view: it never releases the object,
     *     and the owner is still released exactly once at scope exit. */
    dtors = 0;
    {
        owned auto x = new Box(33);
        auto z = readonly x;             // borrow
        check(z->v == 33, "(3) readonly view readable");
        check(z->get() == 33, "(3) readonly view method call works");
        check(dtors == 0, "(3) readonly does not release");
    }
    check(dtors == 1, "(3) owner released once despite outstanding view");

    /* (4) Chained move: ownership flows x -> y -> w; only the final owner `w`
     *     releases, and exactly once. */
    dtors = 0;
    {
        owned auto x = new Box(44);
        auto y = move x;                 // x consumed
        auto w = move y;                 // y consumed; w is the sole owner
        check(w->v == 44, "(4) final owner holds the value");
        check(dtors == 0, "(4) nothing released mid-chain");
    }
    check(dtors == 1, "(4) chained move releases exactly once");

    /* (5) `move`/`readonly` are expression-leading soft keywords and `owned`
     *     is a declaration-prefix soft keyword; all remain usable as ordinary
     *     identifiers outside those positions. */
    int owned = 7;
    int a = 5;
    check(a + owned == 12, "(5) keywords usable as plain identifiers");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
