/* sketch-midopt-list-sum.cy — Phase A/B GEN-OPT baseline
 *
 * Minimal List pipeline used to measure midopt dead-method pruning and
 * MIR stats under default vs -fno-midopt / -fno-exceptions / -O2.
 *
 * Run from repo root:
 *   ./bin/classyc -I include -v -fdump-mir-stats sketch/sketch-midopt-list-sum.cy -eg
 *   ./bin/classyc -I include -v -fdump-mir-stats -fno-midopt sketch/sketch-midopt-list-sum.cy -eg
 *   ./bin/classyc -I include -v -fdump-mir-stats -fno-exceptions -O2 sketch/sketch-midopt-list-sum.cy -eg
 *   ./bin/classyc -I include -c -o /tmp/midopt-list.bmir sketch/sketch-midopt-list-sum.cy
 *   # compare: -fno-midopt -c -o /tmp/nomidopt-list.bmir …
 */

#include <stdio.h>
#include "list.h"

int main (void) {
  auto xs = List<int> ();
  int i;
  int s = 0;

  for (i = 0; i < 100; i++)
    xs.Add (i);

  for (auto v in xs)
    s += v;

  printf ("sum=%d count=%d\n", s, xs.Count ());
  /* expected: sum=4950 count=100 */
  if (s != 4950 || xs.Count () != 100) {
    printf ("FAIL\n");
    return 1;
  }
  printf ("PASS\n");
  return 0;
}
