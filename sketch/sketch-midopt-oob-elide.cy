/* sketch-midopt-oob-elide.cy — Phase B P1: const index OOB elision
 *
 * Static array a[3] with constant index in range: midopt sets elide_oob_p
 * so gen omits _safety_trap.  Still runs correctly with -fexceptions.
 *
 *   ./bin/classyc -I include -v sketch/sketch-midopt-oob-elide.cy -eg
 */

#include <stdio.h>

int main (void) {
  int a[3] = {10, 20, 30};
  int x = a[1]; /* const index 1 < 3 → elide OOB under midopt */
  int y = a[0];
  int z = a[2];

  if (x != 20 || y != 10 || z != 30) {
    printf ("FAIL values %d %d %d\n", x, y, z);
    return 1;
  }
  printf ("PASS oob-elide %d %d %d\n", y, x, z);
  return 0;
}
