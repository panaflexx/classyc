/* val-025-safety-guards-ownership.cy — validates the runtime crash-prevention
 * work layered on top of the default-on exception guards:
 *
 *   Gap 2  setjmp/longjmp register clobber: a scalar local/param modified in a
 *          `try` body must hold its updated value when read in a catch handler
 *          (functions containing `try` keep scalars in memory, not registers).
 *   Gap 3  frame leak on non-local exit: a `return`/`break`/`continue` that
 *          leaves a `try` body must pop its exception frame, so a later `throw`
 *          never longjmps into an already-returned function.
 *   Gap 4  broadened guards: signed INT_MIN / -1 overflow (SIGFPE) and null
 *          pointer indexing `p[i]` both become catchable exceptions.
 *   Ownership-directed guard elision: a dereference of an `OS_OWNED` `new`
 *          binding is proven non-null and its null guard elided — behavior must
 *          be unchanged (valid deref works), while an unproven pointer (a
 *          parameter that may be null) still throws.
 *
 * Run (no special flags): ./bin/classyc -g -I include cy-validate/val-025-safety-guards-ownership.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Point {
    int x;
    int y;
    Point(int a, int b) { x = a; y = b; }
    ~Point() {}
    int sum() { return x + y; }
};

/* Gap 3: return out of a try must pop the frame. */
int returns_from_try(int x) {
    try {
        if (x > 0) return 42;   /* non-local exit out of try body */
        throw(RuntimeException, "neg");
    } catch (Exception e) {
        return -1;
    }
    return 0;
}

/* Ownership elision: p is OS_OWNED via new (non-null by OOM check) -> the
   null guard on p->x / p->y is elided, but the result must be correct. */
int owned_deref(void) {
    Point *p = new Point(3, 4);
    int s = p->x + p->y;
    delete p;
    return s;
}

/* Unproven receiver: q is a parameter that may be null -> guard stays. */
int param_deref(Point *q) {
    return q->x;
}

int main() {
    printf("=== val-025 safety guards + ownership elision ===\n\n");

    /* Gap 2: local + param modified in try, read in catch. */
    {
        int v = 111;
        char *p = "before";
        try {
            v = 222;
            p = "after";
            throw(RuntimeException, "x");
        } catch (Exception e) {
            check(v == 222, "GAP2: local modified in try survives into catch");
            check(strcmp(p, "after") == 0, "GAP2: pointer modified in try survives into catch");
        }
    }

    /* Gap 3: return-out-of-try popped the frame; a later throw is catchable
       and must not crash into the returned frame. */
    check(returns_from_try(1) == 42, "GAP3: return out of try returns correct value");
    {
        int caught = 0;
        try { throw(RuntimeException, "after returned-out-of try"); }
        catch (Exception e) { caught = 1; }
        check(caught == 1, "GAP3: later throw caught (no longjmp into dead frame)");
    }

    /* Gap 4: signed INT_MIN / -1 overflow -> catchable arithmetic exception. */
    {
        int caught = 0;
        int a = -2147483647 - 1;   /* INT_MIN */
        int b = -1;
        try { int c = a / b; if (c) caught = 0; }
        catch (Exception e) { caught = 1; }
        check(caught == 1, "GAP4: INT_MIN / -1 overflow throws (no SIGFPE)");
    }

    /* Gap 4: null pointer indexing p[i] -> NullException. */
    {
        int caught = 0;
        int *p = 0;
        try { int v = p[3]; if (v) caught = 0; }
        catch (NullException e) { caught = 1; }
        check(caught == 1, "GAP4: null pointer index throws NullException");
    }

    /* Ownership elision: owned-new deref result is correct (guard elided). */
    check(owned_deref() == 7, "OWN: owned new deref correct (null guard elided)");

    /* Unproven param: valid pointer works, null pointer still throws. */
    {
        Point *r = new Point(10, 20);
        check(param_deref(r) == 10, "OWN: unproven param deref works for valid pointer");
        delete r;
    }
    {
        int caught = 0;
        try { int v = param_deref(0); if (v) caught = 0; }
        catch (NullException e) { caught = 1; }
        check(caught == 1, "OWN: unproven param deref still guards null");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
