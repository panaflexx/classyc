/* val-018-owns-transforms.cy — validates that transform methods (Filter, Slice,
 * Copy, Concat) on an OWNING collection return NON-OWNING views, so the shared
 * elements are freed exactly once (by the original owner) and never double-freed.
 *
 * This is the key safety invariant of the .owns() protocol: ownership does NOT
 * propagate through views. Exactly one collection owns each object. */
#include <stdio.h>
#include "list.h"
#include "set.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int dtors = 0;
class Item {
    int id;
    Item(int id) { this->id = id; }
    ~Item() { dtors++; }
    int getId() { return this->id; }
};

int isEven(Item* it) { return it->getId() % 2 == 0; }

int main() {
    printf("=== val-018 owns() + transform views (no double-free) ===\n\n");

    /* List: owning source + non-owning Filter view (by-value RAII shell) */
    {
        int before = dtors;
        List<Item*>* src = new List<Item*>().owns();
        src->Add(new Item(1));
        src->Add(new Item(2));
        src->Add(new Item(3));
        src->Add(new Item(4));

        /* Filter returns a NON-owning by-value view sharing src's pointers. */
        {
            auto evens = src->Filter(isEven);
            check(evens.Count() == 2, "Filter view has 2 elements");
            /* ~evens at scope end must NOT free the shared Items */
        }
        check(dtors == before, "Filter view destroyed with 0 Items freed (non-owning)");

        delete src;     /* the sole owner frees all 4 */
        check(dtors - before == 4, "deleting owning source freed all 4 Items");
    }

    /* List: owning source + non-owning Slice/Copy views */
    {
        int before = dtors;
        List<Item*>* src = new List<Item*>().owns();
        src->Add(new Item(10));
        src->Add(new Item(20));
        src->Add(new Item(30));

        {
            auto sl = src->Slice(0, 2);
            auto cp = src->Copy();
            check(sl.Count() == 2 && cp.Count() == 3, "Slice/Copy views populated");
            /* value views end with scope; non-owning of pointees */
        }
        check(dtors == before, "Slice + Copy views freed 0 Items");

        delete src;  /* owner frees 3 */
        check(dtors - before == 3, "owning source freed all 3 Items");
    }

    /* Set: owning source + non-owning Union/Intersect views */
    {
        int before = dtors;
        Item* a = new Item(100);
        Item* b = new Item(200);
        Item* c = new Item(300);

        Set<Item*>* s1 = new Set<Item*>().owns();
        s1->Add(a); s1->Add(b);
        Set<Item*>* s2 = new Set<Item*>();   /* non-owning: shares b, owns nothing */
        s2->Add(b); s2->Add(c);

        {
            auto uni = s1->Union(s2);        /* non-owning by-value view */
            auto inter = s1->Intersect(s2);  /* non-owning by-value view */
            check(uni.Count() == 3 && inter.Count() == 1, "Union/Intersect views correct");
        }
        delete s2;   /* non-owning: frees nothing */
        check(dtors == before, "non-owning sets/views freed 0 Items");

        delete s1;   /* owns a, b -> frees 2 */
        check(dtors - before == 2, "owning set freed its 2 Items");
        delete c;    /* c was never owned by a collection */
        check(dtors - before == 3, "manually freed the unowned Item");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
