/* bugs/013-owned-after-try.cy
 *
 * An `owned` binding declared AFTER a try/catch in the same function
 * silently loses its auto-release: the ownership pass's flow analysis
 * marks it "disposed (state=Unowned)" (visible with -fownership-report),
 * no `delete` is synthesized, and the object leaks on the NORMAL path —
 * no throw involved.  Expected output: ~Box(5) ran / dtor_count=1.
 *
 * Workaround: move the owned binding into its own function (a function
 * whose only try/catch contains the binding analyzes correctly).
 *
 * Pre-existing on HEAD (verified 2026-08-15, same behavior with and
 * without the defer-across-throw shadow stack).
 */

#include <stdio.h>

int dtor_count = 0;

class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { dtor_count++; }
};

int main() {
    try {
        throw(RuntimeException, "x");
    } catch (Exception e) {
    }
    {
        owned auto c = new Box(5);   // leaks: ~Box never runs
    }
    printf("dtor_count=%d (want 1)\n", dtor_count);
    return dtor_count == 1 ? 0 : 1;
}
