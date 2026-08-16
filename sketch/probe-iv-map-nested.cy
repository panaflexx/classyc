/* probe-iv-map-nested.cy — R1 sanity for Map KeyAt/ValAt and nested loops. */
#include <stdio.h>
#include "map.h"
#include "list.h"

int main() {
    auto m = Map<String, int>();
    m["a"] = 10; m["b"] = 20; m["c"] = 30;

    long s = 0;
    for (int i = 0; i < m.Count(); i++) {
        String k = m.KeyAt(i);
        s += m.ValAt(i) + (int)k.length();
    }
    printf("map scan sum=%ld (expect %ld)\n", s, (long)(10+20+30 + 1+1+1));

    /* nested loops, inner uses its own IV bound */
    auto grid = List<int>();
    for (int i = 0; i < 12; i++) grid.Add(i);
    long tot = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            tot += grid.Get(c) * 1;
        }
    }
    printf("nested tot=%ld (expect %ld)\n", tot, (long)(0+1+2+3)*3);

    /* mutation between bound capture and loop: shrink → guard must stay,
       and result must still be correct */
    auto ys = List<int>();
    for (int i = 0; i < 6; i++) ys.Add(i * 2);
    int n = ys.Count();
    ys.RemoveAt(5);            /* shrink after n captured */
    long t = 0;
    for (int i = 0; i < n - 1; i++) t += ys.Get(i);   /* n-1: within new length */
    printf("after-shrink t=%ld (expect %ld)\n", t, (long)(0+2+4+6+8));
    return 0;
}
