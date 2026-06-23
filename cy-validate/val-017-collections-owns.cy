/* val-017-collections-owns.cy — validates the .owns() ownership protocol across
 * List<T*>, Set<T*>, and Map<K,V*>. An owning collection auto-deletes its
 * pointer elements when the collection itself is deleted, via the is_pointer<T>
 * compiler intrinsic + the _owns_ptrs flag. Non-owning (default) collections
 * leave pointed-to objects alone. By-value collections are unaffected. */
#include <stdio.h>
#include "list.h"
#include "set.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int item_dtors = 0;
class Item {
    int id;
    Item(int id) { this->id = id; }
    ~Item() { item_dtors++; }
    int getId() { return this->id; }
};

int main() {
    printf("=== val-017 collection .owns() auto-cleanup ===\n\n");

    /* ── List<Item*>.owns() ──────────────────────────────────────────── */
    printf("List<Item*>.owns():\n");
    {
        List<Item*>* owned = new List<Item*>().owns();
        owned->Add(new Item(1));
        owned->Add(new Item(2));
        owned->Add(new Item(3));
        int before = item_dtors;
        delete owned;
        check(item_dtors - before == 3, "owning List ran 3 element dtors");
    }

    /* ── List<Item*>() non-owning leaves objects alone ───────────────── */
    {
        List<Item*>* plain = new List<Item*>();
        Item* keep = new Item(99);
        plain->Add(keep);
        int before = item_dtors;
        delete plain;
        check(item_dtors == before, "non-owning List ran 0 element dtors");
        delete keep;  /* manual cleanup */
    }

    /* ── Set<Item*>.owns() ───────────────────────────────────────────── */
    printf("\nSet<Item*>.owns():\n");
    {
        Set<Item*>* owned = new Set<Item*>().owns();
        owned->Add(new Item(10));
        owned->Add(new Item(20));
        int before = item_dtors;
        delete owned;
        check(item_dtors - before == 2, "owning Set ran 2 element dtors");
    }

    /* ── Map<int, Item*>.ownsValues() ────────────────────────────────── */
    printf("\nMap<int, Item*>.ownsValues():\n");
    {
        Map<int, Item*>* owned = new Map<int, Item*>().ownsValues();
        owned->Set(1, new Item(100));
        owned->Set(2, new Item(200));
        owned->Set(3, new Item(300));
        int before = item_dtors;
        delete owned;
        check(item_dtors - before == 3, "owning Map ran 3 value dtors");
    }

    /* ── List<int> by-value unchanged ────────────────────────────────── */
    printf("\nList<int> (by-value, sanity):\n");
    {
        List<int>* nums = new List<int>();
        nums->Add(42);
        nums->Add(99);
        delete nums;
        check(1, "List<int> compiles and runs (no element dtors)");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
