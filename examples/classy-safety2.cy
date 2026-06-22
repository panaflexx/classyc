/* classy-safety2.cy — OOM, double-free, and use-after-free guards.
 *
 * When -fexceptions is active the compiler emits safety guards for:
 *   - new OOM          : malloc failure throws RuntimeException("out of memory")
 *   - double-free      : delete of null / already-deleted ptr throws RuntimeException
 *   - use-after-free   : ptr->field after delete throws RuntimeException
 *
 * Build & run:
 *   classyc examples/classy-safety2.cy -eg
 */

#include <stdio.h>

/* ── shared helper ────────────────────────────────────────────────────── */
void caught(const char *what, Exception e) {
    printf("  CAUGHT %s: id=%u msg=\"%s\"\n", what, e.id, e.msg);
}

/* ── a simple class used throughout ──────────────────────────────────── */
class Node {
    int val;
    Node(int v) { val = v; }
    int get() { return val; }
};

/* ── 1. Normal new / delete (no exception expected) ──────────────────── */
void test_normal() {
    printf("[1] normal new / delete (no exception)\n");
    Node *n = new Node(42);
    printf("  n->get() = %d\n", n->get());
    delete n;
    printf("  deleted successfully\n");
}

/* ── 2. delete null → RuntimeException ───────────────────────────────── */
void test_delete_null() {
    printf("[2] delete of null pointer\n");
    Node *p = 0;
    try {
        delete p;
        printf("  ERROR: should not reach here\n");
    } catch (Exception e) {
        caught("RuntimeException", e);
    }
}

/* ── 3. double-free via alias ───────────────────────────────────────── */
/* Note: same-variable double-free is caught by null-out (test [4]).
   Alias double-free requires a full malloc intercept to detect reliably;
   without that, the best protection is the null check on the deleted var. */
void test_double_free() {
    printf("[3] double-free via same variable (null-out catches it)\n");
    Node *p = new Node(7);
    delete p;             /* p is nulled out after delete */
    try {
        delete p;         /* p == NULL now → null guard in cy_safe_free fires */
        printf("  ERROR: should not reach here\n");
    } catch (Exception e) {
        caught("RuntimeException (null after delete)", e);
    }
}

/* ── 4. delete + use of the same variable ──────────────────────────── */
void test_double_free_same_var() {
    printf("[4] same-variable UAF: null-out converts it to null-deref\n");
    Node *p = new Node(3);
    delete p;             /* p gets nulled out after delete */
    try {
        int v = p->get(); /* p == NULL → null guard fires before field access */
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (NullException e) {
        caught("NullException (use after delete)", e);
    }
}

/* ── 5. alias use-after-free: limitation note ───────────────────────── */
/* Without intercepting ALL malloc calls (including runtime-internal ones like
   List<String>* from split()), a per-deref registry check produces false
   positives when non-tracked allocations reuse freed addresses.
   For comprehensive alias UAF detection, compile with ASAN:
     classyc -fexceptions my.cy -eg   (adds null/bounds/div-zero guards)
     clang  -fsanitize=address ...    (adds full UAF/double-free detection)
   Same-variable cases are covered by the null-out mechanism (tests 3 & 4). */
void test_use_after_free() {
    printf("[5] alias UAF: not auto-detected (requires global malloc intercept)\n");
    Node *p = new Node(99);
    Node *q = p;          /* alias */
    delete p;             /* p is nulled; q still has the old (freed) address */
    /* We do NOT access q here since that's UB; we just note the limitation. */
    printf("  (p is now null; q has stale address — see classy-safety2.cy note)\n");
    /* Prevent compiler from optimising q away */ (void) q;
}

/* ── 6. use-after-free: same variable (null-out turns it into null deref) */
void test_uaf_same_var() {
    printf("[6] use-after-free via same variable (null-out → null deref)\n");
    Node *p = new Node(5);
    delete p;             /* p gets nulled out */
    try {
        int v = p->get(); /* p == NULL → NullException fires */
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (NullException e) {
        caught("NullException (null after delete)", e);
    }
}

/* ── 7. normal use with multiple nodes ───────────────────────────────── */
void test_chain() {
    printf("[7] multi-node chain (no exception)\n");
    Node *a = new Node(1);
    Node *b = new Node(2);
    Node *c = new Node(3);
    printf("  a=%d b=%d c=%d\n", a->get(), b->get(), c->get());
    delete c;
    delete b;
    delete a;
    printf("  all deleted successfully\n");
}

/* ── 8. OOM simulation: use-after-free after a full allocate+free cycle  */
/* (We can't easily force malloc to fail, so just verify guard path works.) */
void test_safe_guard_path() {
    printf("[8] guard passes cleanly for live object\n");
    Node *p = new Node(123);
    /* Multiple field accesses — each triggers the deref guard internally */
    for (int i = 0; i < 5; i++) {
        int v = p->get();
        if (v != 123) printf("  ERROR: unexpected value %d\n", v);
    }
    printf("  5 guarded accesses OK, val=%d\n", p->get());
    delete p;
}

int main() {
    printf("=== ClassyC OOM / double-free / use-after-free guards ===\n\n");

    test_normal();
    test_delete_null();
    test_double_free();
    test_double_free_same_var();
    test_use_after_free();
    test_uaf_same_var();
    test_chain();
    test_safe_guard_path();

    printf("\n=== done ===\n");
    return 0;
}
