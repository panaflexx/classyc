/* dict value -> scalar via assignment, init, and return */
#include <stdio.h>

dict d = { "name": "zed", "count": 9, "sub": { "x": 5 } };

char *get_name() { return d.name; }   /* return dict value as char* */
int get_count() { return d.count; }   /* return dict value as int */

int main() {
    char *n = d.name;                 /* init from dict value */
    int c;
    c = d.count;                      /* assign from dict value */
    printf("init n=%s assign c=%d\n", n, c);
    printf("ret name=%s ret count=%d\n", get_name(), get_count());
    dict s = d.sub;                   /* keep box: dict-to-dict */
    printf("sub.x=%d\n", s.x);
    return 0;
}
