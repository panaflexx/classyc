/* Test 118: Complex typedef and type alias scenarios */
#include <stdio.h>

typedef int Integer;
typedef Integer* IntPtr;
typedef int (*Callback)(int, int);
typedef struct { int x; int y; } Point2D;

class Container<T> {
    T data;
    Container(T d) { this.data = d; }
    T get() { return this.data; }
};

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

int main() {
    // Typedef usage
    Integer i = 42;
    IntPtr ip = &i;
    printf("typedef: %d\n", *ip);

    // Function pointer typedef
    Callback cb = add;
    printf("callback add: %d\n", cb(10, 5));
    cb = mul;
    printf("callback mul: %d\n", cb(10, 5));

    // Struct typedef
    Point2D p = { .x = 10, .y = 20 };
    printf("point2d: %d, %d\n", p.x, p.y);

    // Generic with typedef
    Container<Integer>* ci = new Container<Integer>(100);
    Container<Point2D>* cp = new Container<Point2D>({ .x = 5, .y = 15 });
    printf("container int: %d\n", ci->get());
    printf("container point: %d, %d\n", cp->get().x, cp->get().y);

    // Array of function pointers
    Callback callbacks[] = { add, mul };
    for (auto c in callbacks) printf("array cb: %d\n", c(3, 4));

    return 0;
}
