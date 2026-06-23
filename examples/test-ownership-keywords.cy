/* test-ownership-keywords.cy — verifies the three new arena-ownership keywords
 *
 *   detach  — escape a value from the current scope's arena (active today)
 *   attach  — adopt a value into the current scope's arena (stub today)
 *   unowned — opt a declaration out of future auto-defer-delete (parsed today)
 *
 * Run with:  bin/classyc -I include examples/test-ownership-keywords.cy -eg
 */

#include <stdio.h>

/* ---------- detach: String escape ---------- *
 * Without `detach`, a String built in a helper is tracked by the helper's
 * function-level arena; returning it is already protected by the existing
 * `release_keeping` logic at N_RETURN.  But once we start having callers
 * stash it long-term (e.g. into a heap class field across iterations), we
 * want a way to escape the value explicitly.  `detach` makes that explicit.
 */
String build_label(int i) {
    /* `detach` here is redundant for `return` itself (release_keeping covers
     * it) but exercises the parse + codegen path on a String value: it must
     * still produce the correct string. */
    return detach (String)"x#" + i;
}

/* ---------- detach: pointer-to-class escape ---------- */
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() {}
};

Box* make_box(int v) {
    /* `new Box(v)` is not arena-tracked (objects produced by `new` are user-
     * owned today), so `detach` here is a no-op pass-through: the same pointer
     * value falls out.  Exercises the TM_PTR branch of the gen. */
    return detach new Box(v);
}

/* ---------- attach: stub ---------- *
 * Today this should parse cleanly, type-check, and emit no runtime call.
 * Useful only as a forward-compatibility signal in user code.
 */
void demo_attach(Box* b) {
    attach b;            /* statement: stub. */
    printf("  attach: b->v = %d\n", b->v);
}

/* ---------- unowned: declaration opt-out (parsed; semantic no-op today) ---------- */
void demo_unowned() {
    /* These declarations should parse and behave exactly like ordinary ones
     * for now.  When auto-defer-delete lands, `unowned` will suppress it. */
    unowned auto resp_status = 200;      /* primitive, no future cleanup either */
    unowned int counter = 0;
    unowned Box* manual = new Box(99);    /* I own it; I delete it. */
    counter = counter + 1;
    printf("  unowned: status=%d counter=%d manual->v=%d\n",
           resp_status, counter, manual->v);
    delete manual;
}

/* ---------- `attach` and `unowned` are still usable as identifiers ---------- *
 * `detach` becomes an EXPRESSION-level keyword once added, so in an
 * expression context `detach + foo` always means "detach the value `+foo`"
 * (same shadowing rule that `new` has).  `attach` and `unowned`, by
 * contrast, are only special at statement-start and declaration-start
 * respectively, so they remain usable as identifiers in expressions.
 */
int attach_unowned_as_identifier_test() {
    int attach = 11;
    int unowned = 13;
    return attach + unowned;          /* 11 + 13 = 24 */
}

int main() {
    printf("ownership-keyword smoke test\n");

    /* detach on a String: result must still be a valid printable String. */
    String s = build_label(42);
    printf("  detach String  : %s\n", s);

    /* detach on a Box*: the returned pointer is usable; we must delete it. */
    Box* b = make_box(7);
    printf("  detach Box*    : v=%d\n", b->v);
    demo_attach(b);
    delete b;

    /* unowned demo */
    demo_unowned();

    /* `attach`/`unowned` remain usable as identifiers in expressions. */
    int n = attach_unowned_as_identifier_test();
    printf("  identifier use : sum=%d (want 24)\n", n);
    if (n != 24) {
        printf("FAIL: identifier-use sum was %d, expected 24\n", n);
        return 1;
    }
    printf("OK\n");
    return 0;
}
