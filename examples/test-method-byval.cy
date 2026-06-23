/* test-method-byval.cy — method returning a class by value */
#include <stdio.h>

class Item { int v; };

class Box {
    Item items[3];
    Box() {
        this->items[0].v = 10;
        this->items[1].v = 20;
        this->items[2].v = 30;
    }
    Item Get(int i) { return this->items[i]; }
};

int main() {
    Box b = Box();
    Item x = b.Get(1);
    printf("x.v = %d\n", x.v);
    return x.v == 20 ? 0 : 1;
}
