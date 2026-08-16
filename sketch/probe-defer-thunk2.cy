#include <stdio.h>
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { printf("~Box(%d)\n", v); }
};
int main() {
    owned auto b = new Box(2);
    printf("in main\n");
    return 0;
}
