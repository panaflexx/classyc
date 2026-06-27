/* test-interface-bad.c — Phase 1 negative test.
 *
 * @expect: fail
 *
 * `Broken impl Greeter` is missing rank(), so the compiler must reject it with
 * a precise diagnostic, e.g.:
 *
 *   class Broken does not satisfy interface Greeter: missing rank()
 *
 * Run manually:  ./bin/classyc -v -d examples/test-interface-bad.c -eg
 */
#include <stdio.h>

interface Greeter {
    void greet();
    int  rank();
}

class Broken impl Greeter {
    int id;
    Broken(int id) { this.id = id; }
    void greet() { printf("hi\n"); }
    /* missing: int rank(); */
};

int main() {
    return 0;
}
