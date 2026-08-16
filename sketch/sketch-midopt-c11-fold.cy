/* sketch-midopt-c11-fold.cy — pre-MIR C11 folds (dead-arm keep + strlen)
 *
 *   ./bin/classyc -I include -v sketch/sketch-midopt-c11-fold.cy -eg
 *
 * OnlyInDeadArm / OnlyInGenericDefault must be midopt-dead.
 * OnlyInLiveArm / OnlyInGenericInt must be kept.
 */
#include <stdio.h>
#include <string.h>

class FoldBox {
  void OnlyInDeadArm () { printf ("DEAD\n"); }
  void OnlyInLiveArm () { printf ("LIVE\n"); }
  void OnlyInGenericInt () { printf ("GINT\n"); }
  void OnlyInGenericDefault () { printf ("GDEF\n"); }
};

int main (void) {
  FoldBox b;
  int runtime = 1;

  if (0)
    b.OnlyInDeadArm ();
  else
    b.OnlyInLiveArm ();

  if (strlen ("") == 0)
    b.OnlyInLiveArm ();
  else
    b.OnlyInDeadArm ();

  if (strcmp ("ab", "ab") == 0)
    b.OnlyInLiveArm ();

  /* runtime && 0 is always false after evaluating runtime — then-arm dead. */
  if (runtime && 0)
    b.OnlyInDeadArm ();

  _Generic (0, int: (b.OnlyInGenericInt (), 0), default: (b.OnlyInGenericDefault (), 1));

  if (strlen ("hi") != 2) {
    printf ("FAIL strlen fold\n");
    return 1;
  }
  if (strcmp ("a", "b") == 0) {
    printf ("FAIL strcmp fold\n");
    return 1;
  }
  printf ("PASS\n");
  return 0;
}
