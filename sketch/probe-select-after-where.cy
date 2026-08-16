#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    auto xs = List<int>();
    xs.Add(1); xs.Add(2); xs.Add(3);
    auto ev = xs.Where((int x) => x > 0);
    auto d = ev.Select<int>(times2);
    printf("d=%d\n", d.Count());
    return 0;
}
