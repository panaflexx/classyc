/* test-stacktrace.cy
   @expect: crash   (intentionally dereferences null to demo the stack trace) */
#include <stdio.h>

class Point {
    int x, y;

    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }

    int sum() {
        return this.x + this.y;
    }

    int crash_me(int depth) {
        /* Dereference a null pointer to trigger SIGSEGV */
        int *bad = (int *)0;
        return *bad + depth;
    }
};

int helper(Point *p, int n) {
    return p->crash_me(n);
}

int main(int argc, char **argv) {
    Point *p = new Point(10, 20);
    printf("sum = %d\n", p->sum());
    printf("about to crash...\n");
    int result = helper(p, 42);
    printf("result = %d\n", result);  /* never reached */
    return 0;
}
