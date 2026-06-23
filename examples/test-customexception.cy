/* test-customexception.cy — verify user-defined exception via enum works.
 *
 * Users can create their own exceptions with:
 *     enum { MyKeyError = 100, MyParseError = 101 };
 *
 * and then use throw(MyKeyError, msg) / catch(MyKeyError e).
 *
 * Build & run:
 *   ./bin/classyc -g examples/test-customexception.cy -eg
 */

#include <stdio.h>

enum {
    MyKeyError   = 100,
    MyParseError = 101
};

int main() {
    printf("=== user-defined exception test ===\n");

    /* 1. Throw and catch a user-defined exception. */
    printf("[1] custom exception (MyKeyError)\n");
    try {
        throw(MyKeyError, "key 'foo' not present");
    } catch (MyKeyError e) {
        printf("    caught MyKeyError: id=%u msg=\"%s\"\n", e.id, e.msg);
    }

    /* 2. Multiple custom exceptions + catch-all. */
    printf("\n[2] two custom exceptions + catch-all\n");
    for (int i = 0; i < 3; i++) {
        try {
            if (i == 0) throw(MyKeyError,   "missing key");
            if (i == 1) throw(MyParseError, "bad syntax");
            /* i == 2: no throw */
            printf("    case %d: normal completion\n", i);
        } catch (MyKeyError e) {
            printf("    case %d: MyKeyError id=%u\n", i, e.id);
        } catch (MyParseError e) {
            printf("    case %d: MyParseError id=%u\n", i, e.id);
        } catch (Exception e) {
            printf("    case %d: other id=%u\n", i, e.id);
        }
    }

    /* 3. Uncaught custom exception (should abort with numeric id). */
    /* Commented out by default — uncomment to observe the diagnostic.
    printf("\n[3] uncaught custom (expect abort)\n");
    throw(MyKeyError, "this one escapes");
    */

    printf("\n=== done ===\n");
    return 0;
}
