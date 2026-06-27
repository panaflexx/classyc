// test-owned-errors.cy - negative cases for the managed-ownership layer.
//   bin/classyc -I include examples/test-owned-errors.cy -eg
//
// @expect: fail   (each function below is a deliberate ownership violation)

#include <stdio.h>

class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() {}
};

void double_move() {
    owned auto x = new Box(1);
    auto y = move x;       // ok: x -> y
    auto z = move x;       // ERROR: x already moved (use of moved value)
    (void) y; (void) z;
}

void delete_moved() {
    owned auto x = new Box(2);
    auto y = move x;       // x is now a view
    delete x;              // ERROR: cannot delete a moved-from view
    (void) y;
}

void redundant_delete() {
    owned auto x = new Box(3);
    delete x;              // WARNING: redundant; compiler already releases it
}

int main() { return 0; }
