/* bugs/004-list-oob-get.cy
 *
 * List<T> Get(i) with i beyond length – classic OOB.
 * Uses the idiomatic ClassyC brace-init + defer.
 */

#include <stdio.h>
#include "list.h"

int main() {
    List<int>* xs = new List<int>{42};
    defer delete xs;

    int v = xs->Get(5);   // OOB – length is 1
    printf("%d\n", v);
    return 0;
}
