/* Test 106: Exception with custom types and catch ordering */
#include <stdio.h>

class MyError {
    int code;
    String msg;
    MyError(int c, String m) { this.code = c; this.msg = m; }
};

int risky(int n) {
    if (n < 0) throw(RuntimeException, "negative");
    if (n == 0) throw(NullException, "zero");
    if (n > 100) throw(OutOfBoundsException, "too large");
    return n * 2;
}

int main() {
    int tests[] = {-1, 0, 50, 150};

    for (auto n in tests) {
        try {
            int r = risky(n);
            printf("risky(%d) = %d\n", n, r);
        } catch (NullException e) {
            printf("caught NullException: %s\n", e.msg);
        } catch (OutOfBoundsException e) {
            printf("caught OutOfBoundsException: %s\n", e.msg);
        } catch (RuntimeException e) {
            printf("caught RuntimeException: %s\n", e.msg);
        } catch (Exception e) {
            printf("caught base Exception: id=%u, msg=%s\n", e.id, e.msg);
        }
    }

    return 0;
}
