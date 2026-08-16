#include <stdio.h>
#include "list.h"
interface Shape { double area(); }
class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};
/* Workaround: caller builds the collection, callee only returns bare handles. */
Any<Shape>* makeShape(double s) {
    return any<Shape>(new Square(s));
}
int main() {
    auto shapes = new List<Any<Shape>*>();
    shapes->Add(makeShape(2.0));
    shapes->Add(makeShape(3.0));
    double total = 0;
    for (auto h in shapes) total += h->area();
    printf("total=%f (expect 13.0)\n", total);
    delete shapes;
    return total == 13.0 ? 0 : 1;
}
