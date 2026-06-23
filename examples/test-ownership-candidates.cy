/* test-ownership-candidates.cy — exercises the auto-defer-candidate detection.
 *
 * Run with:    bin/classyc -I include -v examples/test-ownership-candidates.cy -eg
 * Look for:    [ownership] auto-defer candidate: <name>  (file:line)
 *
 * Expected verbose output: TWO candidates (`a` and `d`).  The `unowned`-marked
 * binding `b` must NOT appear.  The bare `int` and the in-arg `e` must NOT
 * appear (no resource acquired).
 */

#include <stdio.h>

class Widget {
    int n;
    Widget(int n) { this.n = n; }
    ~Widget() {}
};

int main() {
    Widget* a = new Widget(1);           /* CANDIDATE: bare `new` bound to local */
    unowned Widget* b = new Widget(2);   /* NOT a candidate: user opted out */
    int c = 3;                            /* NOT a candidate: no resource */
    auto d = new Widget(4);              /* CANDIDATE: `auto` + new still counts */
    Widget* e = a;                       /* NOT a candidate: borrowed from `a` */

    printf("a=%d b=%d c=%d d=%d e=%d\n", a->n, b->n, c, d->n, e->n);

    delete a;                            /* manual: today still required */
    delete b;
    delete d;

    return 0;
}
