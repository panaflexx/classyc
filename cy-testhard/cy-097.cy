/* Test 097: Interface with multiple implementations and Any<> */
#include <stdio.h>

interface Drawable {
    void draw();
};

class Circle impl Drawable {
    int radius;
    Circle(int r) { this.radius = r; }
    void draw() { printf("Circle(%d)\n", this.radius); }
};

class Square impl Drawable {
    int side;
    Square(int s) { this.side = s; }
    void draw() { printf("Square(%d)\n", this.side); }
};

void drawAll(List<Any<Drawable>*> shapes) {
    for (auto s in shapes) s->draw();
}

int main() {
    List<Any<Drawable>*> shapes = {
        any<Drawable>(new Circle(5)),
        any<Drawable>(new Square(10)),
        any<Drawable>(new Circle(3))
    };
    drawAll(shapes);
    return 0;
}
