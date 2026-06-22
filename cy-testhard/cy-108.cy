/* Test 108: Nested classes and inner class access */
#include <stdio.h>

class Outer {
    int outer_val;

    Outer(int v) { this.outer_val = v; }

    class Inner {
        int inner_val;
        Outer* parent;

        Inner(Outer* p, int v) { this.parent = p; this.inner_val = v; }

        int getSum() { return this.inner_val + this.parent->outer_val; }
        void print() { printf("Outer: %d, Inner: %d, Sum: %d\n", this.parent->outer_val, this.inner_val, this.getSum()); }
    };

    Inner* createInner(int v) { return new Inner(this, v); }
};

int main() {
    Outer* o = new Outer(10);
    Outer::Inner* i1 = o->createInner(5);
    Outer::Inner* i2 = o->createInner(20);

    i1->print();
    i2->print();

    // Multiple outer instances
    Outer* o2 = new Outer(100);
    Outer::Inner* i3 = o2->createInner(50);
    i3->print();

    return 0;
}
