#include <stdio.h>
#include "list.h"
int times2(int x) { return x * 2; }
int main() {
    List<int>* warm = new List<int>{1};
    auto d0 = warm->Select<int>(times2);
    printf("heap select ok %d\n", d0.Count());
    defer delete warm;

    auto xs = List<int>();
    xs.Add(1); xs.Add(2);
    auto d = xs.Select<int>(times2);
    printf("stack select ok %d\n", d.Count());
    return 0;
}
