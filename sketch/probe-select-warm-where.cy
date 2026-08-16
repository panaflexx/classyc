#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    auto xs = List<int>();
    xs.Add(1); xs.Add(2);
    auto w = xs.Where((int x) => x > 0);
    auto d = xs.Select<int>(times2);
    printf("d=%d w=%d\n", d.Count(), w.Count());
    return 0;
}
