#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    List<int>* xs = new List<int>();
    xs.Add(1); xs.Add(2); xs.Add(3);
    auto d = xs->Select<int>(times2);
    printf("d=%d first=%d\n", d.Count(), d.Get(0));
    defer delete xs;
    return 0;
}
