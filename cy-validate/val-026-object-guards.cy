/* REQUIRES: -fobject-guards */
/* val-026-object-guards.cy — validates the opt-in side-table object guards
 * (-fobject-guards): a layout-preserving use-after-free / double-free detector
 * for `new` class objects, wired to the ownership pass's DEREF_GUARD_CHECK
 * (MaybeOwned) dereference sites.
 *
 * The key case: an object freed through a *function* (or any path that does
 * not null the caller's local) leaves a non-null dangling pointer that the
 * delete-null-out mitigation and the static analysis cannot catch.  With the
 * object guards on, dereferencing it throws a catchable use-after-free instead
 * of reading freed memory (undefined behavior).
 *
 * Run: ./bin/classyc -g -fobject-guards -I include cy-validate/val-026-object-guards.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Box { int v; Box(int a){v=a;} ~Box(){} };

/* Frees the object but does NOT null the caller's local. */
void dispose(Box *b) { delete b; }

/* p conditionally disposed via dispose() -> MaybeOwned at the deref, p not
   nulled: only the object-liveness guard can catch the UAF on cond==1. */
int cond_use(int cond) {
    Box *p = new Box(5);
    if (cond) dispose(p);
    int r = p->v;
    return r;
}

/* Double free at runtime.  The object is held in an array slot (not a tracked
   candidate), so the static analysis can't prove the double free and it reaches
   the runtime guard.  (A same-variable double free is caught at COMPILE time,
   which is even better, but wouldn't exercise the runtime path here.) */
int double_free(void) {
    Box *slot[1];
    slot[0] = new Box(9);
    dispose(slot[0]);
    dispose(slot[0]);   /* second free of the same live-then-dead address */
    return 0;
}

int main() {
    printf("=== val-026 object guards (-fobject-guards) ===\n\n");

    /* 1. Safe path: object never disposed -> deref returns the real value,
       and the guard must NOT false-positive. */
    check(cond_use(0) == 5, "live object deref returns correct value (no false UAF)");

    /* 2. Use-after-free via release-through-function is caught. */
    {
        int caught = 0;
        try { int r = cond_use(1); if (r) caught = 0; }
        catch (Exception e) { caught = (strcmp(e.msg, "use-after-free") == 0); }
        check(caught == 1, "use-after-free (release via fn, non-null dangling) is caught");
    }

    /* 3. Double free is caught. */
    {
        int caught = 0;
        try { double_free(); }
        catch (Exception e) { caught = (strcmp(e.msg, "double free of object") == 0); }
        check(caught == 1, "double free of the same object is caught");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
