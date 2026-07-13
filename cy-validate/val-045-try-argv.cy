/* val-045-try-argv.cy — try/setjmp must not corrupt adjusted array parameters.
 *
 * Regression for the classy-curl bug: `try` forces scalar params into memory
 * so they survive setjmp/longjmp.  `char *argv[]` is an *adjusted* array
 * parameter (stored as `char **` but with `type->arr_type` set).  force_val
 * used to take the address of the stack slot (&argv) whenever arr_type was
 * set, so argv[i] and (void*)argv were garbage inside (and even before) the
 * try body.  Fixed in force_val + N_IND base materialization.
 *
 * Self-contained: builds its own argv vectors so `run-validate.sh` (which
 * invokes `-eg` with no program args) still exercises the bug fully.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-045-try-argv.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* try in the same function as a char *argv[] parameter: this is the shape that
   forced params into memory and corrupted adjusted-array loads. */
int inspect_with_try(int argc, char *argv[]) {
    int ok = 0;
    /* Outside the try body too: reg_p=0 for the whole function once a try exists. */
    check(argc >= 3, "inspect argc >= 3 (outside try)");
    check(argv != NULL, "inspect argv non-NULL (outside try)");
    check(strcmp(argv[1], "foo") == 0, "inspect argv[1] == foo (outside try)");
    check(strcmp(argv[2], "bar") == 0, "inspect argv[2] == bar (outside try)");

    try {
        check(argc >= 3, "inspect argc >= 3 (inside try)");
        check(argv != NULL, "inspect argv non-NULL (inside try)");
        check(strcmp(argv[1], "foo") == 0, "inspect argv[1] == foo (inside try)");
        check(strcmp(argv[2], "bar") == 0, "inspect argv[2] == bar (inside try)");
        /* Index 0 must also load through the pointer, not &argv. */
        check(strcmp(argv[0], "prog") == 0, "inspect argv[0] == prog (inside try)");
        ok = 1;
    } catch (Exception e) {
        printf("  FAIL  inspect threw id=%u msg=%s\n", e.id, e.msg);
        failed++;
        return 0;
    }
    return ok;
}

/* Another callee shape: adjusted array param, no outer locals intervening. */
void mut_index(char *argv[], int i, const char *expect) {
    try {
        check(strcmp(argv[i], expect) == 0, "mut_index argv[i] matches");
    } catch (Exception e) {
        printf("  FAIL  mut_index threw id=%u msg=%s\n", e.id, e.msg);
        failed++;
    }
}

int main(void) {
    printf("=== val-045 try + argv (adjusted array params) ===\n\n");

    /* Synthetic argv — never depends on classyc program-args after -eg. */
    char *args[4];
    args[0] = "prog";
    args[1] = "foo";
    args[2] = "bar";
    args[3] = NULL;

    inspect_with_try(3, args);
    mut_index(args, 1, "foo");
    mut_index(args, 2, "bar");

    /* try in main + true local array decay must still use address-of storage. */
    try {
        char buf[4];
        buf[0] = 'x';
        buf[1] = 0;
        check(buf[0] == 'x', "local array element under try");
        check(strcmp(buf, "x") == 0, "local array decays to pointer under try");

        /* Local pointer array (not an adjusted param): loads must still work. */
        char *local_argv[3];
        local_argv[0] = "prog";
        local_argv[1] = "foo";
        local_argv[2] = "bar";
        check(strcmp(local_argv[1], "foo") == 0, "local pointer-array index under try");
        check(strcmp(local_argv[2], "bar") == 0, "local pointer-array index [2] under try");
    } catch (Exception e) {
        printf("  FAIL  main try threw id=%u msg=%s\n", e.id, e.msg);
        failed++;
    }

    printf("\n=== result: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
