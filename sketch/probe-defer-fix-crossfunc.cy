#include <stdio.h>
interface Shape { double area(); }
class Square {
    double s;
    Square(double s) { this.s = s; }
    double area() { return s * s; }
    ~Square() { printf("~Square ran\n"); }
};
void deepHelper() {
    Any<Shape>* h = any<Shape>(new Square(2.0));
    String s = (String)"deep-" + 1;
    printf("deep: area=%f s=%s\n", h->area(), (char*)s);
    middleHelper();
}
void middleHelper() {
    String s2 = (String)"middle";
    printf("middle: %s\n", (char*)s2);
    throw(RuntimeException, "deep failure");
}
int main() {
    try {
        deepHelper();
    } catch (Exception e) {
        printf("caught: %s\n", (char*)e.msg);
    }
    printf("after\n");
    return 0;
}
