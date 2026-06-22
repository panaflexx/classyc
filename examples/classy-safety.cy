/* classy-safety.cy — JIT safety guards test.
 *
 * When -fexceptions is active, the compiler emits guards for:
 *   - null pointer dereference  (*ptr  /  ptr->field)
 *   - integer division by zero  (a/b, a%b, a/=b, a%=b)
 *   - array/slice out-of-bounds (arr[i]  for C arrays and filter/map slices)
 *
 * Build & run:
 *   classyc examples/classy-safety.cy -eg
 * Disable guards (unsafe, may crash):
 *   classyc examples/classy-safety.cy -eg -fno-exceptions
 */

#include <stdio.h>

/* ── helpers ────────────────────────────────────────────────────────────── */

void caught(const char *what, Exception e) {
    printf("  CAUGHT %s: id=%u msg=\"%s\"\n", what, e.id, e.msg);
}

/* ── 1. null pointer dereference (*ptr) ─────────────────────────────────── */
void test_null_deref() {
    printf("[1] null pointer dereference (*ptr)\n");
    int *p = 0;
    try {
        int v = *p;   // guard fires: NullException
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (NullException e) {
        caught("NullException", e);
    }
}

/* ── 2. null pointer field access (ptr->field) ──────────────────────────── */

struct Point { int x; int y; };

void test_null_field() {
    printf("[2] null pointer field access (ptr->field)\n");
    struct Point *p = 0;
    try {
        int v = p->x;   // guard fires
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (NullException e) {
        caught("NullException", e);
    }
}

/* ── 3. integer division by zero ────────────────────────────────────────── */
void test_div_zero() {
    printf("[3] integer division by zero\n");
    int a = 42, b = 0;
    try {
        int r = a / b;   // guard fires
        printf("  ERROR: should not reach here (r=%d)\n", r);
    } catch (Exception e) {
        caught("arithmetic exception", e);
    }
}

/* ── 4. integer modulo by zero ──────────────────────────────────────────── */
void test_mod_zero() {
    printf("[4] integer modulo by zero\n");
    int a = 42, b = 0;
    try {
        int r = a % b;   // guard fires
        printf("  ERROR: should not reach here (r=%d)\n", r);
    } catch (Exception e) {
        caught("arithmetic exception", e);
    }
}

/* ── 5. C array out-of-bounds ────────────────────────────────────────────── */
void test_arr_oob() {
    printf("[5] C array out-of-bounds\n");
    int arr[4] = {10, 20, 30, 40};
    int idx = 7;   /* out-of-bounds */
    try {
        int v = arr[idx];   // guard fires
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (OutOfBoundsException e) {
        caught("OutOfBoundsException", e);
    }
}

/* ── 6. negative C array index ───────────────────────────────────────────── */
void test_arr_neg() {
    printf("[6] negative array index\n");
    int arr[4] = {1, 2, 3, 4};
    int idx = -1;
    try {
        int v = arr[idx];   // (unsigned)(-1) >= 4 → OOB
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (OutOfBoundsException e) {
        caught("OutOfBoundsException", e);
    }
}

/* ── 7. slice out-of-bounds ──────────────────────────────────────────────── */
void test_slice_oob() {
    printf("[7] slice out-of-bounds (filter result)\n");
    int nums[] = {1, 2, 3, 4, 5};
    auto evens = nums.filter((int x) => x % 2 == 0);  /* {2, 4} - 2 elements */
    int idx = 5;  /* OOB */
    try {
        int v = evens[idx];
        printf("  ERROR: should not reach here (v=%d)\n", v);
    } catch (OutOfBoundsException e) {
        caught("OutOfBoundsException", e);
    }
}

/* ── 8. normal safe access (guards should not fire) ─────────────────────── */
void test_safe_access() {
    printf("[8] normal safe accesses (no exception expected)\n");
    int arr[3] = {10, 20, 30};
    int sum = 0;
    for (int i = 0; i < 3; i++) sum += arr[i];
    printf("  sum=%d (expected 60)\n", sum);

    int a = 100, b = 7;
    printf("  100/7=%d  100%%7=%d\n", a/b, a%b);

    struct Point pt = {3, 4};
    struct Point *pp = &pt;
    printf("  pt.x=%d  pp->y=%d\n", pt.x, pp->y);
}

/* ── 9. /= operator by zero ──────────────────────────────────────────────── */
void test_div_assign_zero() {
    printf("[9] /= operator with zero divisor\n");
    int a = 100, b = 0;
    try {
        a /= b;
        printf("  ERROR: should not reach here (a=%d)\n", a);
    } catch (Exception e) {
        caught("arithmetic exception (/=)", e);
    }
}

/* ── 10. uncaught null (no try → abort-style) ────────────────────────────── */
/* Commented out by default: uncomment to see the abort-with-diagnostic.
void test_uncaught_null() {
    int *p = 0;
    int v = *p;  // aborts: "fatal: null-pointer dereference (line N)"
}
*/

int main() {
    printf("=== ClassyC JIT safety guards ===\n\n");

    test_null_deref();
    test_null_field();
    test_div_zero();
    test_mod_zero();
    test_arr_oob();
    test_arr_neg();
    test_slice_oob();
    test_safe_access();
    test_div_assign_zero();

    printf("\n=== done ===\n");
    return 0;
}
