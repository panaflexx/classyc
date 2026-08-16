#include <stdio.h>
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { printf("~Box(%d)\n", v); }
};
int main() {
    Box *x = new Box(1);
    defer delete x;
    printf("in main\n");
    return 0;
}
