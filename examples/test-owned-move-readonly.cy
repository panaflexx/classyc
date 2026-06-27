/* test-owned-move-readonly.cy — smoke test for the managed-ownership layer
 *
 *   owned <decl>     — opt a binding into single-owner, move-only, auto-managed
 *   move <expr>      — transfer ownership; CONSUMES the source (it goes dead)
 *   readonly <expr>  — borrow a non-owning read-only view
 *
 * Run:  bin/classyc -I include examples/test-owned-move-readonly.cy -eg
 */

#include <stdio.h>

class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { printf("  ~Box(%d)\n", this.v); }
};

int main() {
    printf("owned/move/readonly smoke test\n");

    owned auto x = new Box(1);     // x is the single owner
    auto y = move x;               // ownership x -> y; x is now dead (consumed)
    auto z = readonly y;           // z is a non-owning view of the new owner y

    /* `move` consumes its source: reading `x->v` here would be a compile-time
     * `use of moved value` error.  Read through the new owner `y` (or take a
     * `readonly` view) instead. */
    printf("  y->v = %d (owner after move)\n", y->v);
    printf("  z->v = %d (readonly view of y)\n", z->v);

    /* `move`/`readonly` are EXPRESSION-leading soft keywords (like `detach`):
     * they only shadow identifiers when they START an expression.  As a plain
     * declared variable used in non-leading position they behave normally.
     * `owned` is a declaration-prefix soft keyword, so it is freely usable as
     * an identifier in expressions (like `unowned`). */
    int a = 3, owned = 5;
    int sum = a + owned;                   // 3 + 5 = 8
    printf("  identifier use: sum=%d (want 8)\n", sum);
    if (sum != 8) { printf("FAIL\n"); return 1; }

    printf("OK\n");
    return 0;
}
