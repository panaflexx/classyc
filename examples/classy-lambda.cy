/* classy-lambda.c — test basic typed lambda (anonymous function) syntax.
 *
 * Non-capturing lambdas lower to static named functions (thin C pointers):
 *
 *   (type param, ...) => expr         expression lambda (implicit return)
 *   (type param, ...) => { stmts }    block lambda
 *   ()                => expr         zero-arg lambda
 *
 * Capturing lambdas (free outer locals) are open-coded when used as a direct
 * argument to List/Map/Set HOFs (Where/Filter/Map/ForEach/Any/All) — see
 * LAMBDA-CAPTURE.md and cy-validate/val-042-lambda-capture.cy.  Assigning a
 * capturing lambda to a variable is an error in v1.
 */
#include <stdio.h>

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        passed++;
    } else {
        printf("  FAIL  %s\n", label);
        failed++;
    }
}

/* Higher-order function: apply f to x */
int apply_int(int x, int (*f)(int)) {
    return f(x);
}

/* Call with no args */
int call0(int (*f)(void)) {
    return f();
}

/* Apply predicate */
int count_if(int *arr, int n, int (*pred)(int)) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (pred(arr[i])) c++;
    return c;
}

/* Sort with comparator (simple bubble sort) */
void sort_with(int *arr, int n, int (*cmp)(int, int)) {
    for (int i = 0; i < n; i++)
        for (int j = i+1; j < n; j++)
            if (cmp(arr[j], arr[i]) < 0) {
                int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }
}

int main() {
    printf("=== lambda test suite ===\n\n");
    passed = 0; failed = 0;

    /* ---- expression lambdas ---- */
    printf("-- expression lambdas --\n");

    int r = apply_int(5, (int x) => x * 2);
    check(r == 10,  "1a  (int x) => x*2 applied to 5");

    r = apply_int(7, (int x) => x + 3);
    check(r == 10,  "1b  (int x) => x+3 applied to 7");

    r = apply_int(-4, (int x) => x < 0 ? -x : x);
    check(r == 4,   "1c  abs lambda");

    /* ---- block lambdas ---- */
    printf("\n-- block lambdas --\n");

    r = apply_int(4, (int n) => {
        int s = 0;
        for (int i = 1; i <= n; i++) s += i;
        return s;
    });
    check(r == 10,  "2a  block lambda sum 1..4");

    /* ---- zero-arg lambda ---- */
    printf("\n-- zero-arg lambda --\n");

    r = call0(() => { return 42; });
    check(r == 42,  "3a  zero-arg lambda returns 42");

    /* ---- lambda as stored pointer ---- */
    printf("\n-- lambda stored as pointer --\n");

    int (*double_it)(int) = (int x) => x * 2;
    check(double_it(6) == 12,  "4a  stored lambda double_it(6)");
    check(double_it(0) == 0,   "4b  stored lambda double_it(0)");

    /* ---- lambda in higher-order call ---- */
    printf("\n-- count_if with lambda predicate --\n");

    int arr[] = {1, -2, 3, -4, 5};
    int neg = count_if(arr, 5, (int x) => x < 0);
    check(neg == 2,  "5a  count negatives == 2");

    int pos = count_if(arr, 5, (int x) => x > 0);
    check(pos == 3,  "5b  count positives == 3");

    /* ---- sort comparator lambda ---- */
    printf("\n-- sort with comparator lambda --\n");

    int data[] = {5, 2, 8, 1, 9, 3};
    sort_with(data, 6, (int a, int b) => a - b);
    check(data[0] == 1 && data[5] == 9,  "6a  sorted ascending");

    /* Sort descending */
    int data2[] = {5, 2, 8, 1, 9, 3};
    sort_with(data2, 6, (int a, int b) => b - a);
    check(data2[0] == 9 && data2[5] == 1,  "6b  sorted descending");

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
