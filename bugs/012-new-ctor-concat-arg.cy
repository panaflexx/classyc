/* bugs/012-new-ctor-concat-arg.cy
 *
 * String concatenation as a constructor argument aborts the COMPILER:
 *   new Named((String)"item-" + 1, 9)
 * kills classyc with "ERROR: invalid r->code = 131" (N_CONCAT reaching the
 * statement dispatch in check) + abort().  The same expression outside a
 * `new` argument list is fine, and a non-concat ctor argument is fine.
 *
 * Workaround: bind the concatenation to a variable first:
 *   String label = (String)"item-" + 1;
 *   auto n = new Named(label, 9);
 *
 * Pre-existing on HEAD (verified 2026-08-15); not related to the
 * defer-across-throw shadow stack work.
 */

#include <stdio.h>

class Named {
    String name;
    int id;
    Named(String name, int id) { this.name = name; this.id = id; }
    ~Named() { }
};

int main() {
    auto n = new Named((String)"item-" + 1, 9);  // compiler aborts here
    printf("ok %s\n", (char*)n->name);
    return 0;
}
