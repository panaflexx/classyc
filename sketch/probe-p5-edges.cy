/* probe-p5-edges.cy — BY-VALUE.md P5 edge polish items:
 *  A. arr[i] = T(...)   prvalue assign into a C array of class
 *  B. nested ctor T(-p.getX(), ...)  method call inside ctor args
 *  C. prvalue in f-string / expression chain (aurora-ops dangling note)
 *
 * Run: ./bin/classyc -g -I include sketch/probe-p5-edges.cy -eg
 */
#include <stdio.h>
#include "list.h"

int ctors = 0, dtors = 0;
class Pt {
    int x; int y;
    Pt(int x, int y) { this.x = x; this.y = y; ctors++; }
    ~Pt() { dtors++; }
    int getX() { return x; }
    int getY() { return y; }
};

int main() {
    printf("=== probe P5: edge cases ===\n\n");

    printf("-- A. arr[i] = Pt(...) on C array --\n");
    {
        Pt arr[2];
        arr[0] = Pt(1, 2);
        arr[1] = Pt(3, 4);
        printf("  arr[0]=(%d,%d) arr[1]=(%d,%d)\n",
               arr[0].getX(), arr[0].getY(), arr[1].getX(), arr[1].getY());
    }
    printf("  after: ctors=%d dtors=%d %s\n", ctors, dtors,
           ctors==dtors ? "OK" : "*** IMBALANCE ***");

    printf("-- B. nested ctor Pt(-p.getX(), -p.getY()) --\n");
    {
        int c0 = ctors, d0 = dtors;
        Pt p = Pt(5, 6);
        Pt q = Pt(-p.getX(), -p.getY());
        printf("  q=(%d,%d) expect (-5,-6)\n", q.getX(), q.getY());
        printf("  delta: +ctors=%d +dtors=%d (still in scope)\n",
               ctors-c0, dtors-d0);
    }
    printf("  after: ctors=%d dtors=%d %s\n", ctors, dtors,
           ctors==dtors ? "OK" : "*** IMBALANCE ***");

    printf("-- C. chained call on prvalue: Pt(1,2).getX() --\n");
    {
        int c0 = ctors, d0 = dtors;
        int v = Pt(7, 8).getX();
        printf("  v=%d expect 7; delta: +ctors=%d +dtors=%d %s\n",
               v, ctors-c0, dtors-d0,
               (ctors-c0)==(dtors-d0) ? "OK" : "*** LEAKED temp ***");
    }

    printf("\ntotal ctors=%d dtors=%d %s\n", ctors, dtors,
           ctors==dtors ? "OK" : "*** GLOBAL IMBALANCE ***");
    return 0;
}
lsok
