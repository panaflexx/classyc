#include <stdio.h>
interface Shape { double area(); }
class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};
void allocAndThrow(int i) {
    Any<Shape>* h = any<Shape>(new Square((double)i));
    String s = (String)"iter-" + i;
    (void)h; (void)s;
    throw(RuntimeException, "boom");
}
int main() {
    int caught = 0;
    for (int i = 0; i < 200000; i++) {
        try {
            allocAndThrow(i);
        } catch (Exception e) {
            caught++;
        }
    }
    printf("caught=%d\n", caught);
    return caught == 200000 ? 0 : 1;
}
