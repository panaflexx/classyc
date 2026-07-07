/* val-021-collection-string-ownership.cy
 *
 * Pins the arena/collection ownership boundary for `String` elements.
 *
 * The hazard: a `String` built inside a loop iteration is tracked by the
 * per-iteration String arena.  Storing it into a `List<String>` that was
 * declared OUTSIDE the loop leaves the list holding a pointer that the next
 * `c2m_str_release_to(per_iter_mark)` frees -- a use-after-free when the list
 * is read after the loop.
 *
 * Desired model (move-on-store): adding a heap String to a collection moves it
 * out of the scope arena and the collection becomes its owner, freeing it when
 * the collection is reclaimed.  Literals are copied so the collection always
 * owns a real, freeable buffer.
 *
 * Run:  ./bin/classyc -I include cy-validate/val-021-collection-string-ownership.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Cross-function allocator: returns a freshly-allocated tracked String. */
String label(int i) {
    return (String)"item#" + i;
}

int main(void) {
    printf("=== val-021 collection String ownership ===\n\n");

    /* (1) Strings built in a loop, stored into an OUTER list, must survive the
       per-iteration arena release and remain readable after the loop. */
    List<String>* names = new List<String>();
    defer delete names;
    for (int i = 0; i < 1000; i++) {
        String s = label(i);   /* tracked by the per-iter arena */
        names->Add(detach s);         /* move-on-store: list takes ownership */
    }
    check(names->Count() == 1000, "(1) list holds all 1000 entries");
    check(strcmp(names->Get(0),   "item#0")   == 0, "(1) first entry intact after loop");
    check(strcmp(names->Get(999), "item#999") == 0, "(1) last entry intact after loop");

    /* (2) Literals stored into a list are owned (copied), readable, and freed
       safely on delete (no crash from free()-ing a literal). */
    List<String>* lits = new List<String>();
    lits->Add("alpha");
    lits->Add("beta");
    check(strcmp(lits->Get(0), "alpha") == 0, "(2) literal element readable");
    check(strcmp(lits->Get(1), "beta")  == 0, "(2) literal element readable");
    delete lits;  /* must not crash */
    check(1, "(2) deleting list of literals does not crash");

    /* (3) Map<String,int> string keys built in a loop survive and are findable. */
    Map<String, int>* ages = new Map<String, int>();
    defer delete ages;
    for (int i = 0; i < 500; i++) {
        String k = label(i);
        ages->Set(k, i);
    }
    check(ages->Count() == 500, "(3) map holds all 500 keys");
    check(ages->Get(label(0)) == 0,     "(3) first key findable after loop");
    check(ages->Get(label(499)) == 499, "(3) last key findable after loop");

    /* (4) Copy() of a String list yields independent buffers: deleting the
       copy must not corrupt or double-free the source. */
    List<String>* copy = names->Copy();
    delete copy;  /* must not free names' buffers */
    check(strcmp(names->Get(0), "item#0") == 0, "(4) source intact after copy deleted");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
