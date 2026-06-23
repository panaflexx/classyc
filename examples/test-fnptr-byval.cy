/* test-fnptr-byval.cy — call a function pointer returning a class by value */
#include <stdio.h>

class Track { int id; };

Track bump(Track t) { t.id = t.id + 1; return t; }

int id_of(Track t) { return t.id; }

int use(Track(*fn)(Track), Track x) {
    return id_of(fn(x));
}

int main() {
    Track a; a.id = 41;
    int r = use(bump, a);
    printf("r = %d\n", r);
    return r == 42 ? 0 : 1;
}
