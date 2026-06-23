/* test-byval-eq.cy — == / != on by-value class values (memcmp lowering) */
#include <stdio.h>

class P { int x; int y; };

int main() {
    P a; a.x = 1; a.y = 2;
    P b; b.x = 1; b.y = 2;
    P c; c.x = 3; c.y = 4;

    int eq_ab = (a == b);
    int eq_ac = (a == c);
    int ne_ac = (a != c);

    printf("a==b: %d  a==c: %d  a!=c: %d\n", eq_ab, eq_ac, ne_ac);
    if (eq_ab == 1 && eq_ac == 0 && ne_ac == 1) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
