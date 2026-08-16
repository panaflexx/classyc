#include <stdio.h>
interface Shape { double area(); }
class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() { printf("~Square ran\n"); fflush(stdout); }
};
int main() {
    try {
        Any<Shape>* h = any<Shape>(new Square(2.0));
        printf("area=%f\n", h->area()); fflush(stdout);
        throw(RuntimeException, "oops");
    } catch (Exception e) {
        printf("caught\n"); fflush(stdout);
    }
    printf("after\n"); fflush(stdout);
    return 0;
}
