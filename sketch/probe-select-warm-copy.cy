#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    auto xs = List<int>();
    xs.Add(1); xs.Add(2);
    auto c = xs.Copy();  /* value method first */
    auto d = xs.Select<int>(times2);
    printf("d=%d\n", d.Count());
    return 0;
}
