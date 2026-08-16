/* val-061-atexit-leak-cleanup.cy — a genuinely leaked (unowned, never
 * deleted) Any<I>* collection must be swept by the atexit() safety net
 * without crashing.
 *
 * Regression test for SHORTCOMINGS.md gotcha #9: classyc-driver.c used to
 * fall through from JIT/interp execution into MIR_gen_finish()/MIR_finish()
 * before the real process exit, which unmapped the generated code. libc
 * only runs atexit() callbacks at actual process termination, so by the
 * time cobjarena.h's leak sweep (registered via atexit() from inside the
 * running .cy program) finally ran, its own function pointer -- and the
 * destructor thunks it called -- pointed into unmapped memory, segfaulting
 * instead of just leaking. Fixed by calling exit() immediately after
 * execution finishes, while the JIT-generated code is still live.
 *
 * This is intentionally the "forgot to clean up" case (no .owns(), no
 * delete, no defer): the point is that leaking must be silent, not fatal.
 * Run: ./bin/classyc -g -I include cy-validate/val-061-atexit-leak-cleanup.cy -eg
 */
#include <stdio.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

interface Shape { double area(); }

class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};

int approx_area(List<Any<Shape>*>* shapes) {
    double d = shapes->Get(0)->area() - 4.0;
    if (d < 0) d = -d;
    return d < 0.001;
}

int main() {
    printf("=== val-061 leaked-handle atexit sweep ===\n\n");

    /* Deliberately unowned and never freed: the atexit sweep in
       cobjarena.h is the only thing that will ever reclaim this. Reaching
       the final printf below (and exiting 0) is the actual assertion --
       a regression here shows up as a crash (signal), not a FAIL line. */
    List<Any<Shape>*>* shapes = new List<Any<Shape>*>();
    shapes->Add(any<Shape>(new Square(2.0)));
    check(approx_area(shapes), "area computed before the deliberate leak");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    printf("(shapes intentionally leaked -- atexit sweep must not crash)\n");
    return failed;
}
