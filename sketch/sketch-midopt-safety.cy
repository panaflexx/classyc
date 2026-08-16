/* sketch-midopt-safety.cy — midopt local nullness / interval diagnostics
 *
 * Expect compile-time *warnings* (not runtime traps) for definite bugs:
 *   - null dereference after p = NULL
 *   - OOB index on fixed array with const / tracked local index
 *   - division by zero with const 0
 *
 * Also: proven-safe elision (no crash) for a[0] on int a[4] and this->field.
 *
 * Run:
 *   ./bin/classyc -I include sketch/sketch-midopt-safety.cy -eg
 *   ./bin/classyc -I include -fsafety-errors sketch/sketch-midopt-safety.cy -eg
 *     → should fail compile on definite cases
 */
#include <stdio.h>

class Box {
  int n;
  Box(int n) { this.n = n; }
  int get() { return this.n; } /* this is non-null */
};

int main() {
  int a[4] = {10, 20, 30, 40};
  int i = 0;
  int ok_sum;

  /* Proven in-range: should elide OOB trap */
  ok_sum = a[0] + a[i];
  i = 3;
  ok_sum += a[i];

  /* Proven-safe method call on stack class */
  Box b = Box(7);
  ok_sum += b.get();

  printf("ok_sum=%d\n", ok_sum);

  /* --- Definite bugs (warnings; runtime may still trap if executed) --- */
  /* Keep them under if (0) so -eg still returns 0 for a smoke PASS. */
  if (0) {
    int *p = NULL;
    int z = 0;
    int j = 10;
    int bad;
    bad = *p;           /* definite null deref */
    bad = a[j];         /* definite OOB (j=10, len=4) */
    bad = a[-1];        /* definite OOB */
    bad = 1 / z;        /* definite div0 */
    (void)bad;
  }

  printf("PASS\n");
  return 0;
}
