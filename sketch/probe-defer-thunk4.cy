#include <stdio.h>
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { printf("~Box(%d) ran\n", v); }
};
int main() {
    try {
        auto x = new Box(1);
        defer delete x;
        owned auto b = new Box(2);
        throw(RuntimeException, "oops");
    } catch (Exception e) {
        printf("caught\n");
    }
    printf("done\n");
    return 0;
}
