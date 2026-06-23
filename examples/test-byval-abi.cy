/* test-byval-abi.cy — class passed/returned by value (ABI) */
#include <stdio.h>

class P { int x; int y; };

P padd(P a, P b) {
    P r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    return r;
}

int main() {
    P a; a.x = 1; a.y = 2;
    P b; b.x = 10; b.y = 20;
    P c = padd(a, b);
    printf("c = (%d, %d)\n", c.x, c.y);
    return c.x == 11 && c.y == 22 ? 0 : 1;
}
