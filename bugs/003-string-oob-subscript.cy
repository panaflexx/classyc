/* bugs/003-string-oob-subscript.cy
 *
 * Out-of-bounds Substr on String (start beyond length).
 * Demonstrates missing range check on String operations.
 */

#include <stdio.h>

int main() {
    String s = "hello";          // length 5
    String bad = s.Substr(10, 2); // start=10 > length → OOB
    printf("len=%zu\n", bad.Length());
    return 0;
}
