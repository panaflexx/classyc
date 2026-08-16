#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    auto xs = List<int>();
    xs.Add(1); xs.Add(2);
    auto d = xs.Select<int>(times2);
    printf("d=%d\n", d.Count());
    List<int>* p = &xs;
    auto e = p.Select<int>(times2);
    printf("e=%d first=%d\n", e.Count(), e.Get(0));
    return 0;
}
