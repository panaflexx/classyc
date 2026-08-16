/* val-062-defer-throw.cy — `defer delete` and `owned` cleanup must run when a
 * `throw`'s longjmp skips every syntactic exit point (return / break /
 * continue / fall-through) where the compiler normally emits it.
 *
 * Regression test for SHORTCOMINGS.md gotcha #9 (the defer/owned half; the
 * String/Any<I> arena half was fixed earlier by banked marks).  The fix: a
 * runtime shadow stack of cleanup thunks (cy__defer_stack in cyexc.h).
 * `defer delete x;` / `owned auto x = new C(...)` push (thunk, ptr) at
 * registration — the pointer captured by value, Go-style — the normal exit
 * paths discard the entry as they replay the AST, and the exception-dispatch
 * path invokes every entry registered since the catching try's entry.
 *
 * Also covers the original reparse-corruption crash: `defer delete` of a
 * GENERIC class (Map<String,int>) or an Any<I> handle synthesizes a thunk
 * naming a mangled class (__generic_Map_String_int / __Any_Shape), which used
 * to abort the compiler ("wrong get for token_t") because the mangled name
 * was not a visible parser typename in the reparse scope.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-062-defer-throw.cy -eg
 */
#include <stdio.h>
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Destructor log: every destroyed Box/Sq appends its id; tests reset and
   compare.  Ordering matters — defers run LIFO. */
int dtor_log[32];
int dtor_count = 0;
void log_dtor(int v) { if (dtor_count < 32) dtor_log[dtor_count] = v; dtor_count++; }
void reset_log(void) { dtor_count = 0; }
int log_is(int a, int b, int c) {   /* dtor_count==3 && log == [a,b,c] */
    return dtor_count == 3 && dtor_log[0] == a && dtor_log[1] == b && dtor_log[2] == c;
}

class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { log_dtor(v); }
};

/* String member: exercises the member-drop inside the delete thunk, which the
   dispatch path runs BEFORE releasing the String arena. */
class Named {
    String name;
    int id;
    Named(String name, int id) { this.name = name; this.id = id; }
    ~Named() { log_dtor(id); }
};

interface Shape { double area(); }
class Sq {
    double s;
    Sq(double s) { this.s = s; }
    double area() { return s * s; }
    ~Sq() { log_dtor(5); }
};

enum { MyErr = 100 };

void thrower(void) { throw(RuntimeException, "deep"); }

/* The try that catches is in main, two frames up: this frame's defer is never
   reached by the longjmp, so only the shadow stack can run it. */
void middle(void) {
    Box *x = new Box(2);
    defer delete x;
    Box *y = new Box(3);
    defer delete y;
    thrower();
}

/* `owned` tests live in their own functions: the ownership pass's flow
   analysis currently marks an owned binding Unowned when an earlier try/catch
   exists in the SAME function (a separate, pre-existing bug — see
   bugs/013-owned-after-try.cy), which would mask the throw behavior under
   test here. */
void owned_same_func(void) {
    try {
        owned auto b = new Box(4);
        throw(RuntimeException, "oops");
    } catch (Exception e) {
    }
}

void owned_string_member(void) {
    try {
        String label = (String)"item-" + 1;
        owned auto n = new Named(label, 9);
        throw(RuntimeException, "oops");
    } catch (Exception e) {
    }
}

int main(void) {
    printf("=== val-062 defer/owned cleanup across throw ===\n\n");

    /* 1. Same-function try/throw. */
    reset_log();
    try {
        Box *x = new Box(1);
        defer delete x;
        throw(RuntimeException, "oops");
    } catch (Exception e) {
    }
    check(dtor_count == 1 && dtor_log[0] == 1, "defer delete runs across same-function throw");

    /* 2. Cross-function: defers in an intervening frame, LIFO order. */
    reset_log();
    try {
        middle();
    } catch (Exception e) {
    }
    check(dtor_count == 2 && dtor_log[0] == 3 && dtor_log[1] == 2,
          "defers in unwound frames run, LIFO");

    /* 3. owned auto-release across a throw. */
    reset_log();
    owned_same_func();
    check(dtor_count == 1 && dtor_log[0] == 4, "owned binding released across throw");

    /* 4. Any<I>* handle under defer delete across a throw (dtor slot frees
       the wrapped concrete object). */
    reset_log();
    try {
        Any<Shape> *h = any<Shape>(new Sq(2.0));
        defer delete h;
        throw(RuntimeException, "oops");
    } catch (Exception e) {
    }
    check(dtor_count == 1 && dtor_log[0] == 5, "Any<I>* handle deleted across throw");

    /* 5. Nested try, non-matching inner catch rethrows: inner defers run at
       the inner dispatch, outer defers at the outer dispatch, each exactly
       once. */
    reset_log();
    try {
        Box *a = new Box(6);
        defer delete a;
        try {
            Box *b = new Box(7);
            defer delete b;
            throw(MyErr, "inner");
        } catch (KeyException e) {   /* id 8 != 100: no match -> rethrow */
            log_dtor(999);           /* must not run */
        }
    } catch (Exception e) {
    }
    check(dtor_count == 2 && dtor_log[0] == 7 && dtor_log[1] == 6,
          "nested try + rethrow: inner and outer defers each run once");

    /* 6. defer registered BEFORE the try is below the banked mark: not run by
       the dispatch path, still run at the normal function-scope exit. */
    reset_log();
    {
        Box *pre = new Box(20);
        defer delete pre;
        try {
            throw(RuntimeException, "oops");
        } catch (Exception e) {
        }
        check(dtor_count == 0, "defer before try not fired by caught throw");
    }
    check(dtor_count == 1 && dtor_log[0] == 20, "defer before try runs at scope exit");

    /* 7. No throw: the shadow entry is discarded by the normal replay — the
       cleanup runs exactly once (a double run would double-free trap). */
    reset_log();
    {
        Box *x = new Box(8);
        defer delete x;
    }
    check(dtor_count == 1 && dtor_log[0] == 8, "no-throw path runs cleanup exactly once");

    /* 8. Loop: per-iteration defers run per iteration; the throwing
       iteration's defer runs via the shadow stack; earlier iterations are
       not re-run. */
    reset_log();
    try {
        for (int i = 0; i < 5; i++) {
            Box *x = new Box(10 + i);
            defer delete x;
            if (i == 2) throw(RuntimeException, "stop");
        }
    } catch (Exception e) {
    }
    check(log_is(10, 11, 12), "loop defers: iterations 0,1 normal, 2 via shadow, none twice");

    /* 9. Generic class (reparse-corruption regression): this shape used to
       abort the COMPILER.  Deleting the map frees its buffer; a double run
       would trip the double-free guard. */
    try {
        auto m = new Map<String, int>();
        defer delete m;
        m->Set("a", 1);
        m->Set("b", 2);
        throw(RuntimeException, "oops");
    } catch (Exception e) {
    }
    auto m2 = Map<String, int>();   /* the machinery still works afterwards */
    m2.Set("x", 42);
    check(m2.Get("x") == 42, "generic Map<K,V> defer delete across throw: compiles, survives");

    /* 10. Class with a String member: the thunk's member drop runs before
       the dispatch path releases the String arena. */
    reset_log();
    owned_string_member();
    check(dtor_count == 1 && dtor_log[0] == 9, "String-member class deleted across throw");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
