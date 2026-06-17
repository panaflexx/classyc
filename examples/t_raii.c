#include <stdio.h>

class Obj {
    int id;
    Obj(int id) { this.id = id; printf("ctor %d\n", this.id); }
    ~Obj() { printf("dtor %d\n", this.id); }
    int get() { return id; }
};

class Def {
    int v;
    Def() { this.v = 7; printf("Def ctor\n"); }
    ~Def() { printf("Def dtor\n"); }
};

void nested(int early) {
    Def a;                       /* ctor */
    printf("a.v=%d\n", a.v);
    {
        Def b;                   /* ctor */
        printf("inner b.v=%d\n", b.v);
        if (early) {
            printf("early return\n");
            return;              /* should run b dtor, then a dtor */
        }
    }                            /* b dtor here on normal path */
    printf("after inner block\n");
}                                /* a dtor */

int main() {
    printf("-- nested(0) --\n");
    nested(0);
    printf("-- nested(1) --\n");
    nested(1);
    printf("-- main scope --\n");
    Def x;                       /* ctor, dtor at main exit */
    printf("x.v=%d\n", x.v);
    return 0;
}
