/* classy-exceptions.cy — try / catch / throw exercise.
 *
 * throw(id, msg)        : id is an integer exception class, msg a string.
 * catch(Class var)      : runs when the thrown id matches Class.
 * catch(Exception var)  : base / catch-all clause (also `catch(var)`).
 *
 * The built-in classes (NullException, OutOfBoundsException, ...) and the
 * `Exception` value type come from the compiler — no #include needed.
 *
 * Build & run (JIT):   classyc examples/classy-exceptions.cy -eg
 * Disable exceptions:  classyc examples/classy-exceptions.cy -eg -fno-exceptions
 */

#include <stdio.h>

/* A helper that throws across a call boundary (caught by its caller). */
void risky(int code) {
    if (code == 1) throw(NullException, "null pointer encountered");
    if (code == 2) throw(OutOfBoundsException, "index out of range");
    if (code == 3) throw(RuntimeException, "generic runtime failure");
    printf("    risky(%d): completed normally\n", code);
}

int main() {
    printf("=== ClassyC exceptions ===\n\n");

    /* 1. Basic throw + catch, reading the exception fields. */
    printf("[1] basic throw/catch\n");
    try {
        printf("    in try, throwing...\n");
        throw(NullException, "boom");
    } catch (NullException e) {
        printf("    caught: id=%u msg=\"%s\"\n", e.id, e.msg);
    }

    /* 2. Typed multi-catch: the matching clause is selected by id. */
    printf("\n[2] typed multi-catch (cross-function throw)\n");
    for (int i = 1; i <= 4; i++) {
        try {
            risky(i);
        } catch (NullException e) {
            printf("    [%d] NullException: %s\n", i, e.msg);
        } catch (OutOfBoundsException e) {
            printf("    [%d] OutOfBoundsException: %s\n", i, e.msg);
        } catch (Exception e) {
            printf("    [%d] other (id=%u): %s\n", i, e.id, e.msg);
        }
    }

    /* 3. No clause matches the inner try -> propagate to the outer try. */
    printf("\n[3] propagation through nested try\n");
    try {
        try {
            throw(RuntimeException, "from inner");
        } catch (NullException e) {
            printf("    WRONG: inner should not match\n");
        }
    } catch (Exception e) {
        printf("    outer caught propagated: id=%u msg=\"%s\"\n", e.id, e.msg);
    }

    /* 4. Body completes normally -> handler is skipped. */
    printf("\n[4] normal completion (handler skipped)\n");
    try {
        printf("    in try, no throw\n");
    } catch (Exception e) {
        printf("    WRONG: handler should not run\n");
    }

    printf("\n=== done ===\n");
    return 0;
}
