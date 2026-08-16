#include <stdio.h>
#include "list.h"
interface Shape { double area(); }
class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() {}
};
int main() {
    List<Any<Shape>*>* shapes = new List<Any<Shape>*>();
    defer delete shapes;
    shapes->Add(any<Shape>(new Square(2.0)));
    printf("%f\n", shapes->Get(0)->area());
    return 0;
}
