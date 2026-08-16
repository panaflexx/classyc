/* sketch-midopt-iv-guards.cy — R1 symbolic loop-bound guard elision
 *
 * Patterns that should get elide_oob stamps (see -v elisions count):
 *   A. for (int i = 0; i < xs.Count(); i++) xs.Get(i)      direct Count bound
 *   B. int n = xs.Count(); for (i=0;i<n;i++) xs.Get(i)     bound local
 *   C. for (int i = 0; i < 8; i++) a[i]                    C array, const bound
 *   D. xs[i] subscript in the same counted loop            class subscript
 *   E. hazard: xs.Add inside the loop                      NO elision (safe)
 *   F. hazard: i written inside the body                   NO elision (safe)
 * All loops still produce correct results (checksums).
 */

#include <stdio.h>
#include "list.h"

int main() {
    auto xs = List<int>();
    for (int k = 0; k < 8; k++) xs.Add(k * 10);

    /* A: direct Count bound */
    long sa = 0;
    for (int i = 0; i < xs.Count(); i++) sa += xs.Get(i);
    printf("A sum=%ld (expect 280)\n", sa);

    /* B: bound local */
    int n = xs.Count();
    long sb = 0;
    for (int i = 0; i < n; i++) sb += xs.Get(i);
    printf("B sum=%ld (expect 280)\n", sb);

    /* C: C array, const bound */
    int a[8];
    for (int i = 0; i < 8; i++) a[i] = i + 1;
    long sc = 0;
    for (int i = 0; i < 8; i++) sc += a[i];
    printf("C sum=%ld (expect 36)\n", sc);

    /* D: class subscript */
    long sd = 0;
    for (int i = 0; i < xs.Count(); i++) sd += xs[i];
    printf("D sum=%ld (expect 280)\n", sd);

    /* E: growth in loop is safe for OOB (cond re-evaluated), but the hazard
       scan conservatively keeps guards — result must still be correct. */
    auto ys = List<int>();
    for (int i = 0; i < 4; i++) ys.Add(i);
    for (int i = 0; i < ys.Count(); i++) {
        int v = ys.Get(i);
        if (v == 3) ys.Add(99);
    }
    printf("E count=%d (expect 5)\n", ys.Count());

    /* F: IV written in body — guards stay, result still correct */
    long sf = 0;
    for (int i = 0; i < xs.Count(); i++) {
        sf += xs.Get(i);
        if (i == 2) i = i;   /* self-write: hazard (conservative) */
    }
    printf("F sum=%ld (expect 280)\n", sf);

    /* G: while (i < Count()) { use; i++; } */
    long sg = 0;
    int gi = 0;
    while (gi < xs.Count()) {
        sg += xs.Get(gi);
        gi++;
    }
    printf("G sum=%ld (expect 280)\n", sg);

    /* H: if (i < Count()) after known non-neg i */
    long sh = 0;
    int hi = 3;
    if (hi < xs.Count()) sh += xs.Get(hi);
    printf("H get=%ld (expect 30)\n", sh);

    /* I: i <= Count()-1 */
    long si = 0;
    for (int i = 0; i <= xs.Count() - 1; i++) si += xs[i];
    printf("I sum=%ld (expect 280)\n", si);

    /* J: unique heap pointer receiver */
    owned auto p = new List<int>();
    for (int k = 0; k < 8; k++) p.Add(k * 10);
    long sj = 0;
    for (int i = 0; i < p.Count(); i++) sj += p.Get(i);
    printf("J sum=%ld (expect 280)\n", sj);

    return 0;
}
