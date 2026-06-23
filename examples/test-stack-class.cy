/* test-stack-class.cy — Validates stack allocation and by-value semantics for classes
 *
 * This test ensures that:
 * 1. A class can be constructed on the stack (e.g., `Point p = Point(10, 20);`)
 * 2. The constructor is called with the correct arguments and `this` pointer.
 * 3. The object's fields and methods work correctly.
 * 4. The destructor is automatically called when the variable goes out of scope.
 */

#include <stdio.h>

int dtor_run_count = 0;

class Point {
    int x;
    int y;

    Point(int x, int y) {
        this->x = x;
        this->y = y;
        printf("  [ctor] Point(%d, %d) created at %p\n", this->x, this->y, this);
    }

    ~Point() {
        printf("  [dtor] Point(%d, %d) destroyed at %p\n", this->x, this->y, this);
        dtor_run_count = dtor_run_count + 1;
    }

    int sum() {
        return this->x + this->y;
    }
};

int test_stack_allocation() {
    /* 1. Stack allocation using constructor syntax */
    Point p = Point(10, 20);

    /* 2. Verify state */
    if (p.x != 10 || p.y != 20) {
        printf("  [err] State mismatch: %d, %d\n", p.x, p.y);
        return 0;
    }
    if (p.sum() != 30) {
        printf("  [err] Method call failed: %d\n", p.sum());
        return 0;
    }

    /* 3. Destructor should run automatically right after this return */
    return 1;
}

int main() {
    printf("=== Testing Stack Classes ===\n");

    int ok = test_stack_allocation();

    if (!ok) {
        printf("FAIL: Object state was invalid\n");
        return 1;
    }

    if (dtor_run_count != 1) {
        printf("FAIL: Destructor was not run exactly once (ran %d times)\n", dtor_run_count);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
