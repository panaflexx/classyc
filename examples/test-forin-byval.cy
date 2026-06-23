/* test-forin-byval.cy — for-in over a class returning by-value class elements */
#include <stdio.h>

class Item { int v; };

class Box {
    Item items[3];
    int n;
    Box() {
        this->n = 3;
        this->items[0].v = 10;
        this->items[1].v = 20;
        this->items[2].v = 30;
    }
    int Count() { return this->n; }
    Item Get(int i) { return this->items[i]; }
};

int main() {
    Box b = Box();
    int sum = 0;
    for (auto it in b) {
        sum = sum + it.v;
    }
    printf("sum = %d\n", sum);
    return sum == 60 ? 0 : 1;
}
